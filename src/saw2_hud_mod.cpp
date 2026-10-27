#include "saw2_hud_mod.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

REXCVAR_DEFINE_BOOL(saw2_hud_mod, false, "Mods",
                    "Show host HUD overlay (health, syringes, items)");
REXCVAR_DEFINE_BOOL(saw2_achievements, true, "Mods",
                    "Host achievements: persist unlocks under Documents/saw2/achievements");
REXCVAR_DEFINE_BOOL(saw2_hud_demo, false, "Mods",
                    "Animate demo HUD values (fallback while guest bind is pending)");
REXCVAR_DEFINE_DOUBLE(saw2_hud_debug_health, -1.0, "Mods",
                      "Force health 0..max (-1 = auto)");
REXCVAR_DEFINE_DOUBLE(saw2_hud_debug_health_max, 100.0, "Mods",
                      "Forced / fallback max health");
REXCVAR_DEFINE_INT32(saw2_hud_debug_syringes, -1, "Mods",
                     "Force syringe count (-1 = auto)");

namespace saw2_hud {
namespace {

float ReadBeFloat(const uint8_t* p) {
  uint32_t bits = 0;
  std::memcpy(&bits, p, 4);
  bits = __builtin_bswap32(bits);
  float f = 0.f;
  std::memcpy(&f, &bits, 4);
  return f;
}

float ReadLeFloat(const uint8_t* p) {
  float f = 0.f;
  std::memcpy(&f, p, 4);
  return f;
}

int32_t ReadBeInt(const uint8_t* p) {
  uint32_t bits = 0;
  std::memcpy(&bits, p, 4);
  bits = __builtin_bswap32(bits);
  return static_cast<int32_t>(bits);
}

int32_t ReadLeInt(const uint8_t* p) {
  int32_t v = 0;
  std::memcpy(&v, p, 4);
  return v;
}

bool FiniteOk(float f) { return std::isfinite(f) && f >= 0.f && f < 1.0e6f; }

bool IsPlausibleHealthPair(float cur, float mx) {
  if (!FiniteOk(cur) || !FiniteOk(mx)) return false;
  // Absolute (10..1000) or normalized (≈1.0) max health.
  const bool abs_max = mx >= 10.f && mx <= 1000.f;
  const bool norm_max = mx >= 0.95f && mx <= 1.05f;
  if (!abs_max && !norm_max) return false;
  if (cur > mx + (norm_max ? 0.02f : 1.f)) return false;
  if (norm_max && cur < 0.f) return false;
  return true;
}

struct HealthCandidate {
  uintptr_t addr = 0;  // current HP float/int host pointer
  int max_delta = 4;   // 0 = solo float (max forced 100); else ±offset to max
  bool be = true;
  bool as_int = false;
  float cur = 0.f;
  float max = 100.f;
  float prev_cur = -1.f;
  int changes = 0;  // times cur moved while sticky — live pawn signal
  std::chrono::steady_clock::time_point bound_at{};
};

#if defined(_WIN32)
bool RegionReadable(const MEMORY_BASIC_INFORMATION& mbi) {
  if (mbi.State != MEM_COMMIT) return false;
  if (mbi.Protect & PAGE_GUARD) return false;
  const DWORD prot = mbi.Protect & 0xFF;
  // Prefer writable heaps (live game objects); still allow RO for CDOs.
  return prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_WRITECOPY ||
         prot == PAGE_READONLY || prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_WRITECOPY;
}

bool RegionWritable(const MEMORY_BASIC_INFORMATION& mbi) {
  if (!RegionReadable(mbi)) return false;
  const DWORD prot = mbi.Protect & 0xFF;
  return prot == PAGE_READWRITE || prot == PAGE_EXECUTE_READWRITE || prot == PAGE_WRITECOPY;
}

// memmem for a 4-byte needle; returns first hit or nullptr.
const uint8_t* FindBytes(const uint8_t* hay, size_t hay_n, const uint8_t* needle, size_t nlen) {
  if (nlen == 0 || hay_n < nlen) return nullptr;
  for (size_t i = 0; i + nlen <= hay_n; ++i) {
    if (std::memcmp(hay + i, needle, nlen) == 0) return hay + i;
  }
  return nullptr;
}

int ScanSyringesNear(uint8_t* center, bool be) {
  if (!center) return 0;
  uint8_t* from = center > reinterpret_cast<uint8_t*>(512) ? center - 512 : center;
  uint8_t* to = center + 512;
  int best = 0;
  for (uint8_t* s = from; s + 12 < to; s += 4) {
    const bool tagged = (s + 7 < to && std::memcmp(s, "Syringe", 7) == 0) ||
                        (s + 10 < to && std::memcmp(s, "WP_Syringe", 10) == 0);
    for (int off = 4; off <= 48; off += 4) {
      uint8_t* p = s + off;
      if (p + 4 > to) break;
      int32_t v = 0;
      __try {
        v = be ? ReadBeInt(p) : ReadLeInt(p);
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        continue;
      }
      if (v >= 1 && v <= 9) {
        if (tagged) return v;
        best = std::max(best, v);
      }
    }
  }
  return (best >= 1 && best <= 6) ? best : 0;
}

float ScorePair(float cur, float mx, bool be) {
  float score = 5.f;
  // Xbox guest objects are big-endian — prefer BE strongly.
  if (be) score += 18.f;
  if (std::fabs(mx - 100.f) < 0.05f) score += 50.f;
  else if (std::fabs(mx - 1.f) < 0.02f) score += 40.f;
  else if (std::fabs(mx - 50.f) < 0.05f) score += 22.f;
  else if (std::fabs(mx - 200.f) < 0.05f) score += 18.f;
  else if (mx >= 25.f && mx <= 150.f) score += 10.f;
  if (cur > 0.05f) score += 12.f;
  if (cur >= mx * 0.5f) score += 8.f;
  if (cur >= mx * 0.9f) score += 12.f;
  // Full health is the common spawn state for the local pawn — require this
  // for a confident first bind (avoids sticky junk like 50.1/100 constants).
  if (std::fabs(cur - mx) < 0.02f * std::max(mx, 1.f)) score += 40.f;
  // Half-health constants are extremely common in cooked data — demote.
  if (std::fabs(cur - mx * 0.5f) < 0.15f && std::fabs(cur - mx) > 1.f) score -= 25.f;
  return score;
}

bool LooksFullHealth(float cur, float mx) {
  return IsPlausibleHealthPair(cur, mx) &&
         std::fabs(cur - mx) < 0.02f * std::max(mx, 1.f);
}
#endif

struct ItemSlot {
  std::string name;
  int count = 0;
};

struct HudSnapshot {
  bool valid = false;
  float health = 0.f;
  float health_max = 100.f;
  int syringes = 0;
  ItemSlot items[6]{};
  int item_count = 0;
  const char* source = "none";
};

class HudOverlayDialog final : public rex::ui::ImGuiDialog {
 public:
  HudOverlayDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate,
                   rex::Runtime* runtime)
      : ImGuiDialog(drawer),
        immediate_(immediate),
        runtime_(runtime),
        born_(std::chrono::steady_clock::now()) {
    REXLOG_INFO("HUD: overlay created (mod={} demo={})", REXCVAR_GET(saw2_hud_mod),
                REXCVAR_GET(saw2_hud_demo));
    StartProbe();
  }

  ~HudOverlayDialog() override {
    stop_.store(true, std::memory_order_release);
    if (probe_.joinable()) probe_.join();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    const bool mod_on = REXCVAR_GET(saw2_hud_mod) || FlagTruthy("saw2_hud_mod");
    if (!mod_on) return;

    const auto now = std::chrono::steady_clock::now();
    const auto age = now - born_;
    // Soft delay after boot, then fade in (~0.7s).
    constexpr float kAppearAfter = 8.f;
    constexpr float kFadeSecs = 0.7f;
    const float age_s = std::chrono::duration<float>(age).count();
    if (age_s < kAppearAfter) return;
    const float fade = std::clamp((age_s - kAppearAfter) / kFadeSecs, 0.f, 1.f);
    if (fade <= 0.001f) return;

    // Only during gameplay, and only after guest HP actually binds.
    // Pending used to pulse the fill (looked like HP oscillating in menus).
    if (!gameplay_ok_.load(std::memory_order_relaxed)) return;
    HudSnapshot snap = BuildSnapshot();
    if (!snap.valid) return;

    EnsureTextures();

    const float frac =
        snap.health_max > 0.f ? std::clamp(snap.health / snap.health_max, 0.f, 1.f) : 0.f;

    const float pad_x = 28.f;
    const float pad_y = 96.f;
    const float bar_w = 480.f;
    const float bar_h = 78.f;
    ImGui::SetNextWindowPos(ImVec2(pad_x, io.DisplaySize.y - bar_h - pad_y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(bar_w, bar_h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##saw2_hud", nullptr, flags)) {
      if (hp_frame_ || hp_fill_) {
        DrawHealthBar(frac, bar_w, bar_h, fade);
      } else {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.69f, 0.06f, 0.09f, fade));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.03f, 0.04f, 0.35f * fade));
        ImGui::ProgressBar(frac, ImVec2(bar_w, bar_h * 0.35f), "");
        ImGui::PopStyleColor(2);
      }
    }
    ImGui::End();
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(4);
  }

 private:
  void DrawHealthBar(float frac, float bar_w, float bar_h, float fade) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 frame_max(origin.x + bar_w, origin.y + bar_h);
    const ImU32 tint = IM_COL32(255, 255, 255, static_cast<int>(fade * 255.f + 0.5f));

    // Fill UNDER the frame (hpbarline sits below hpbar art).
    if (hp_fill_ && frac > 0.001f) {
      const float fill_w = bar_w * frac;
      dl->AddImage(reinterpret_cast<ImTextureID>(hp_fill_.get()), origin,
                   ImVec2(origin.x + fill_w, origin.y + bar_h), ImVec2(0.f, 0.f),
                   ImVec2(frac, 1.f), tint);
    }

    // Frame on top (hpbar.png masks / borders the fill).
    if (hp_frame_) {
      dl->AddImage(reinterpret_cast<ImTextureID>(hp_frame_.get()), origin, frame_max,
                   ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), tint);
    }

    ImGui::Dummy(ImVec2(bar_w, bar_h));
  }

  static std::filesystem::path ExeDirectory() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(buf).parent_path();
#else
    return {};
#endif
  }

  std::unique_ptr<rex::ui::ImmediateTexture> LoadPngTexture(
      const std::filesystem::path& path) {
    if (!immediate_ || path.empty()) return nullptr;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return nullptr;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return nullptr;
    const auto size = static_cast<size_t>(file.tellg());
    if (size == 0) return nullptr;
    file.seekg(0);
    std::vector<uint8_t> bytes(size);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
      return nullptr;
    }

    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba =
        rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), width, height);
    if (rgba.empty() || width <= 0 || height <= 0) return nullptr;

    return immediate_->CreateTexture(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                     rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
  }

  void EnsureTextures() {
    if (textures_tried_) return;
    textures_tried_ = true;
    if (!immediate_) {
      REXLOG_WARN("HUD: no ImmediateDrawer — hpbar textures unavailable");
      return;
    }

    const auto exe_dir = ExeDirectory();
    const std::filesystem::path candidates[] = {
        exe_dir / "hpbar.png",
        exe_dir / "assets" / "hpbar.png",
        std::filesystem::path("hpbar.png"),
    };

    for (const auto& frame_path : candidates) {
      auto frame = LoadPngTexture(frame_path);
      if (!frame) continue;
      const auto fill_path = frame_path.parent_path() / "hpbarline.png";
      auto fill = LoadPngTexture(fill_path);
      hp_frame_ = std::move(frame);
      hp_fill_ = std::move(fill);
      REXLOG_INFO("HUD: loaded hpbar from {}", frame_path.string());
      if (!hp_fill_) {
        REXLOG_WARN("HUD: hpbarline.png missing next to {}", frame_path.string());
      }
      return;
    }
    REXLOG_WARN("HUD: hpbar.png / hpbarline.png not found next to exe");
  }

  HudSnapshot BuildSnapshot() {
    HudSnapshot snap;
    snap.health_max = static_cast<float>(REXCVAR_GET(saw2_hud_debug_health_max));
    if (snap.health_max < 1.f) snap.health_max = 100.f;

    const double dbg_h = REXCVAR_GET(saw2_hud_debug_health);
    const int dbg_s = REXCVAR_GET(saw2_hud_debug_syringes);

    HudSnapshot probed;
    {
      std::lock_guard lock(mu_);
      probed = probed_;
    }

    if (dbg_h >= 0.0) {
      snap.valid = true;
      snap.health = static_cast<float>(dbg_h);
      snap.syringes = std::max(0, dbg_s);
      snap.source = "debug";
    } else if (probed.valid) {
      snap = probed;
    } else if (REXCVAR_GET(saw2_hud_demo) || FlagTruthy("saw2_hud_demo")) {
      const auto t =
          std::chrono::duration<float>(std::chrono::steady_clock::now() - born_).count();
      snap.valid = true;
      snap.health = snap.health_max * (0.55f + 0.35f * std::sin(t * 0.7f));
      snap.syringes = 1 + (static_cast<int>(t) % 3);
      snap.items[0] = {"Syringe", snap.syringes};
      snap.items[1] = {"Tape", 1};
      snap.items[2] = {"Bat", 1};
      snap.item_count = 3;
      snap.source = "demo";
    }

    if (dbg_s >= 0) snap.syringes = dbg_s;
    if (snap.valid && snap.syringes > 0) {
      bool have = false;
      for (int i = 0; i < snap.item_count; ++i) {
        if (snap.items[static_cast<size_t>(i)].name.find("Syringe") != std::string::npos) {
          snap.items[static_cast<size_t>(i)].count = snap.syringes;
          have = true;
          break;
        }
      }
      if (!have && snap.item_count < static_cast<int>(std::size(snap.items))) {
        snap.items[static_cast<size_t>(snap.item_count++)] = {"Syringe", snap.syringes};
      }
    }
    return snap;
  }

  void StartProbe() {
    stop_.store(false);
    probe_ = std::thread([this] { ProbeLoop(); });
  }

  void ProbeLoop() {
    for (int i = 0; i < 15 && !stop_.load(); ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

    REXLOG_INFO("HUD: probe thread running runtime={}", static_cast<const void*>(runtime_));
    uint64_t ticks = 0;
    while (!stop_.load()) {
      ++ticks;
      try {
        ScanPass();
      } catch (...) {
        REXLOG_WARN("HUD: probe C++ exception — continuing");
      }
      UpdateGameplayGate();
      if ((ticks % 20) == 0) {
        std::lock_guard lock(mu_);
        REXLOG_INFO("HUD: probe alive ticks={} sticky={:x} health_valid={}", ticks,
                    sticky_.addr, probed_.valid);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }
  }

  rex::Runtime* ResolveRuntime() {
    if (runtime_) return runtime_;
    runtime_ = rex::Runtime::instance();
    return runtime_;
  }

  rex::memory::Memory* ResolveMemory() {
    if (auto* rt = ResolveRuntime()) return rt->memory();
    return nullptr;
  }

  void UpdateGameplayGate() {
    const auto now = std::chrono::steady_clock::now();
    WorldHints hints;
    HudSnapshot probed;
    {
      std::lock_guard lock(mu_);
      hints = hints_;
      probed = probed_;
    }

    const bool loading_fresh =
        hints.loading_seen && (now - hints.last_loading) < std::chrono::seconds(3);
    const bool cutscene_fresh =
        hints.cutscene_seen && (now - hints.last_cutscene) < std::chrono::seconds(3);

    // Prefer level over menu by timestamp (both strings can coexist in memory).
    const bool in_level = hints.level_seen && (hints.last_level >= hints.last_menu);
    // Health bind is NOT required to show (fill stays full until bound).
    const bool ok = in_level && !loading_fresh && !cutscene_fresh;
    const bool prev = gameplay_ok_.exchange(ok, std::memory_order_relaxed);
    if (ok != prev) {
      REXLOG_INFO("HUD: gameplay={} (level={} menu={} load={} cut={} health={})", ok, in_level,
                  hints.menu_seen, loading_fresh, cutscene_fresh, probed.valid);
    }
  }

  void ScanPass() {
#if defined(_WIN32)
    auto* mem = ResolveMemory();
    uint8_t* phys = mem ? mem->physical_membase() : nullptr;
    if (!phys) {
      if ((scan_window_++ % 32) == 0) REXLOG_WARN("HUD: physical_membase null");
      return;
    }
    if (!runtime_) runtime_ = ResolveRuntime();

    HealthCandidate sticky{};
    {
      std::lock_guard lock(mu_);
      sticky = sticky_;
    }
    if (sticky.addr) {
      RereadSticky(sticky);
      {
        std::lock_guard lock(mu_);
        sticky = sticky_;
      }
      // False-positive constants never change — drop and keep searching.
      if (sticky.addr && sticky.changes == 0) {
        const auto age = std::chrono::steady_clock::now() - sticky.bound_at;
        if (age > std::chrono::seconds(12)) {
          REXLOG_WARN("HUD: sticky @{:x} frozen at {:.1f}/{:.1f} for {}s — rescanning",
                      sticky.addr, sticky.prev_cur, sticky.max,
                      std::chrono::duration_cast<std::chrono::seconds>(age).count());
          std::lock_guard lock(mu_);
          sticky_ = {};
          probed_ = {};
          sticky = {};
        }
      }
      if (sticky.addr) {
        ScanWorldMarkersOnly(mem);
        return;
      }
    }

    // CRITICAL: only touch physical_membase via VirtualQuery + SafeCopy.
    // SearchAligned / TranslateVirtual on unmapped guest VA (e.g. 0x40000000)
    // trips ReX VEH → "Unhandled guest access violation" and kills the title.
    // UE3 heap data lives in the upper half of physical RAM — scan that first.
    constexpr size_t kPhysSize = 0x20000000u;
    constexpr size_t kWindow = 0x00400000u;
    const size_t nwin = kPhysSize / kWindow;
    // Rotate starting at 0x10000000 (window 64) where live objects showed up.
    const size_t win_i = static_cast<size_t>((scan_window_++ + 64) % nwin);
    uint8_t* start = phys + win_i * kWindow;
    uint8_t* end = start + kWindow;

    static const uint8_t kBe100[] = {0x42, 0xC8, 0x00, 0x00};
    static const uint8_t kBe50[] = {0x42, 0x48, 0x00, 0x00};
    static const uint8_t kBe1[] = {0x3F, 0x80, 0x00, 0x00};
    static const uint8_t kBe200[] = {0x43, 0x48, 0x00, 0x00};
    static const uint8_t kBeI100[] = {0x00, 0x00, 0x00, 0x64};
    static const uint8_t kBeI50[] = {0x00, 0x00, 0x00, 0x32};

    HealthCandidate best{};
    float best_score = -1.f;
    int regions = 0;
    int hits = 0;
    int str_hits = 0;
    int nz_pages = 0;
    int pair_hits = 0;
    std::vector<uint8_t> page(0x1000);

    if ((scan_window_ % 16) == 1) {
      REXLOG_INFO("HUD: scan phys+0x{:x} host={}", win_i * kWindow,
                  static_cast<const void*>(start));
    }

    uint8_t* p = start;
    while (p < end && !stop_.load()) {
      MEMORY_BASIC_INFORMATION mbi{};
      if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) break;
      uint8_t* region = static_cast<uint8_t*>(mbi.BaseAddress);
      uint8_t* region_end = region + mbi.RegionSize;
      if (region_end <= start) {
        p = region_end;
        continue;
      }
      if (region >= end) break;

      if (RegionReadable(mbi)) {
        uint8_t* from = region < start ? start : region;
        uint8_t* to = region_end > end ? end : region_end;
        if (to > from + 16) {
          ++regions;
          for (uint8_t* page_ptr = from; page_ptr < to && !stop_.load(); page_ptr += 0x1000) {
            const size_t n = std::min<size_t>(0x1000, static_cast<size_t>(to - page_ptr));
            if (!SafeCopy(page.data(), page_ptr, n)) continue;

            str_hits += ScanMarkersInBuffer(page.data(), n, page_ptr);

            bool nz = false;
            for (size_t i = 0; i < n; i += 64) {
              if (page[i] | page[std::min(i + 1, n - 1)] | page[std::min(i + 2, n - 1)] |
                  page[std::min(i + 3, n - 1)]) {
                nz = true;
                break;
              }
            }
            if (!nz) continue;
            ++nz_pages;

            if (!RegionWritable(mbi)) continue;

            auto consider_float = [&](const uint8_t* pat, float maxv) {
              const uint8_t* cur = page.data();
              const uint8_t* lim = page.data() + n;
              int local = 0;
              while (cur + 4 <= lim && local < 64) {
                const uint8_t* hit = FindBytes(cur, static_cast<size_t>(lim - cur), pat, 4);
                if (!hit) break;
                ++hits;
                ++local;
                ConsiderAnchor(page_ptr + (hit - page.data()), maxv, true, best, best_score);
                cur = hit + 4;
              }
            };
            auto consider_int = [&](const uint8_t* pat, float maxv) {
              const uint8_t* cur = page.data();
              const uint8_t* lim = page.data() + n;
              int local = 0;
              while (cur + 4 <= lim && local < 32) {
                const uint8_t* hit = FindBytes(cur, static_cast<size_t>(lim - cur), pat, 4);
                if (!hit) break;
                ++hits;
                ++local;
                ConsiderIntAnchor(page_ptr + (hit - page.data()), maxv, true, best, best_score);
                cur = hit + 4;
              }
            };

            consider_float(kBe100, 100.f);
            consider_float(kBe50, 50.f);
            consider_float(kBe200, 200.f);
            consider_float(kBe1, 1.f);
            consider_int(kBeI100, 100.f);
            consider_int(kBeI50, 50.f);

            for (size_t i = 0; i + 8 <= n; i += 4) {
              const float a = ReadBeFloat(page.data() + i);
              const float b = ReadBeFloat(page.data() + i + 4);
              if (!IsPlausibleHealthPair(a, b)) continue;
              if (b < 10.f && !(b >= 0.95f && b <= 1.05f && a >= 0.5f)) continue;
              ++pair_hits;
              const float score = ScorePair(a, b, true);
              if (score > best_score) {
                best_score = score;
                best = {};
                best.addr = reinterpret_cast<uintptr_t>(page_ptr + i);
                best.max_delta = 4;
                best.be = true;
                best.cur = a;
                best.max = b;
              }
            }
          }
        }
      }

      if (region_end <= p)
        p += 0x1000;
      else
        p = region_end;
    }

    bool level = false;
    {
      std::lock_guard lock(mu_);
      level = hints_.level_seen;
    }
    REXLOG_INFO(
        "HUD: scan phys+0x{:x} regions={} hits={} pairs={} nz={} str={} best={:.1f} level={}",
        win_i * kWindow, regions, hits, pair_hits, nz_pages, str_hits, best_score, level);

    auto publish_scaled = [&](HealthCandidate c, float score) {
      float cur = c.cur;
      float mx = c.max;
      if (mx > 0.9f && mx < 1.1f) {
        cur *= 100.f;
        mx = 100.f;
      }
      c.prev_cur = cur;
      c.bound_at = std::chrono::steady_clock::now();
      c.changes = 0;
      const int syr = ScanSyringesNear(reinterpret_cast<uint8_t*>(c.addr), c.be);
      {
        std::lock_guard lock(mu_);
        sticky_ = c;
      }
      PublishGuest(cur, mx, syr);
      REXLOG_INFO("HUD: BOUND health {:.1f}/{:.1f} ({}{} @{:x}) score={:.0f} syr={}", cur, mx,
                  c.be ? "BE" : "LE", c.as_int ? "/i" : "", c.addr, score, syr);
    };

    if (best_score >= 40.f && best.addr) {
      const bool full = LooksFullHealth(best.cur, best.max);
      bool confirmed = false;
      bool decreased = false;
      for (const auto& prev : prev_cands_) {
        if (prev.addr != best.addr || prev.be != best.be || prev.as_int != best.as_int) continue;
        if (std::fabs(prev.max - best.max) > 0.1f) continue;
        if (std::fabs(prev.cur - best.cur) < 0.5f) confirmed = true;
        if (best.cur < prev.cur - 0.5f) decreased = true;  // took damage → real pawn
      }
      prev_cands_.push_back(best);
      if (prev_cands_.size() > 12) prev_cands_.erase(prev_cands_.begin());

      // Instant-bind full HP; or same addr that dropped (damage proof).
      if ((full && best_score >= 90.f) || (confirmed && full) || decreased) {
        publish_scaled(best, best_score);
      } else {
        REXLOG_INFO("HUD: candidate {:.1f}/{:.1f} score={:.0f} full={} — confirm", best.cur,
                    best.max, best_score, full);
      }
    }
#else
    (void)runtime_;
#endif
  }

#if defined(_WIN32)
  void ScanWorldMarkersOnly(rex::memory::Memory* mem) {
    if (!mem || !mem->physical_membase()) return;
    // Cheap 256 KB slice of physical RAM for level/menu path strings.
    constexpr size_t kSlice = 0x00040000u;
    const size_t off = (static_cast<size_t>(scan_window_++) * kSlice) % 0x20000000u;
    uint8_t* start = mem->physical_membase() + off;
    std::vector<uint8_t> page(0x1000);
    for (size_t i = 0; i < kSlice; i += 0x1000) {
      if (!SafeCopy(page.data(), start + i, 0x1000)) continue;
      ScanMarkersInBuffer(page.data(), 0x1000, start + i);
    }
  }

  // Copy guest bytes into a private buffer. Failures return false (no thread kill).
  static bool SafeCopy(uint8_t* dst, const uint8_t* src, size_t n) {
    if (!dst || !src || n == 0) return false;
    __try {
      std::memcpy(dst, src, n);
      return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return false;
    }
  }

  // Search a private buffer; guest_base is the host address of buffer[0] (for logging only).
  int ScanMarkersInBuffer(const uint8_t* buf, size_t n, uint8_t* /*guest_base*/) {
    int hits = 0;
    const auto now = std::chrono::steady_clock::now();
    auto has = [&](const char* s) -> bool {
      const size_t len = std::strlen(s);
      return FindBytes(buf, n, reinterpret_cast<const uint8_t*>(s), len) != nullptr;
    };

    // Package / movie stems as they appear in guest paths (both cases).
    const bool level = has("LvlA_") || has("LvlB_") || has("LvlC_") || has("LvlD_") ||
                       has("LvlE_") || has("LvlF_") || has("LvlG_") || has("LvlTapp") ||
                       has("lvlA_") || has("lvlB_") || has("lvlC_") || has("lvlD_") ||
                       has("lvlE_") || has("lvlF_") || has("lvlG_") || has("lvlTapp") ||
                       has("CookedXenon\\Lvl") || has("CookedXenon/Lvl");
    if (level) {
      std::lock_guard lock(mu_);
      hints_.level_seen = true;
      hints_.last_level = now;
      ++hits;
    }
    if (has("SawEntry") || has("sawentry") || has("SawEntry.xxx")) {
      std::lock_guard lock(mu_);
      hints_.menu_seen = true;
      hints_.last_menu = now;
      ++hits;
    }
    if (has("Loading1") || has("loading1")) {
      std::lock_guard lock(mu_);
      hints_.loading_seen = true;
      hints_.last_loading = now;
      ++hits;
    }
    if (has("Movies\\lvl") || has("Movies/lvl") || has("Movies\\Lvl") || has("Movies/Lvl")) {
      std::lock_guard lock(mu_);
      hints_.cutscene_seen = true;
      hints_.last_cutscene = now;
      ++hits;
    }
    return hits;
  }

  void ConsiderAnchor(uint8_t* max_ptr, float expect_max, bool be, HealthCandidate& best,
                      float& best_score) {
    float mx = 0.f, before = 0.f, after = 0.f;
    __try {
      mx = be ? ReadBeFloat(max_ptr) : ReadLeFloat(max_ptr);
      before = be ? ReadBeFloat(max_ptr - 4) : ReadLeFloat(max_ptr - 4);
      after = be ? ReadBeFloat(max_ptr + 4) : ReadLeFloat(max_ptr + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return;
    }
    if (std::fabs(mx - expect_max) > 0.1f) return;

    auto take = [&](float cur, int max_delta, float bias) {
      if (!IsPlausibleHealthPair(cur, mx)) return;
      float score = ScorePair(cur, mx, be) + bias;
      if (score <= best_score) return;
      best_score = score;
      best = {};
      // max at max_ptr; cur is immediately before (Health,HealthMax) or after.
      best.addr = reinterpret_cast<uintptr_t>(max_delta > 0 ? max_ptr - 4 : max_ptr + 4);
      best.max_delta = max_delta;
      best.be = be;
      best.as_int = false;
      best.cur = cur;
      best.max = mx;
    };
    // UE3 APawn typically lays out Health then HealthMax → cur immediately before max.
    take(before, 4, 2.f);
    take(after, -4, 0.f);
  }

  void ConsiderIntAnchor(uint8_t* max_ptr, float expect_max, bool be, HealthCandidate& best,
                         float& best_score) {
    int32_t mx = 0, before = 0, after = 0;
    __try {
      mx = be ? ReadBeInt(max_ptr) : ReadLeInt(max_ptr);
      before = be ? ReadBeInt(max_ptr - 4) : ReadLeInt(max_ptr - 4);
      after = be ? ReadBeInt(max_ptr + 4) : ReadLeInt(max_ptr + 4);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      return;
    }
    if (mx != static_cast<int32_t>(expect_max)) return;
    auto take = [&](int32_t cur, int max_delta, float bias) {
      if (cur < 0 || cur > mx) return;
      const float fcur = static_cast<float>(cur);
      const float fmx = static_cast<float>(mx);
      float score = ScorePair(fcur, fmx, be) + bias - 4.f;  // slight prefer floats
      if (score <= best_score) return;
      best_score = score;
      best = {};
      best.addr = reinterpret_cast<uintptr_t>(max_delta > 0 ? max_ptr - 4 : max_ptr + 4);
      best.max_delta = max_delta;
      best.be = be;
      best.as_int = true;
      best.cur = fcur;
      best.max = fmx;
    };
    take(before, 4, 5.f);
    take(after, -4, 3.f);
  }

  bool RereadSticky(const HealthCandidate& sticky_in) {
    HealthCandidate sticky = sticky_in;
    auto* p = reinterpret_cast<uint8_t*>(sticky.addr);
    MEMORY_BASIC_INFORMATION mbi{};
    if (!(VirtualQuery(p, &mbi, sizeof(mbi)) && RegionReadable(mbi))) {
      std::lock_guard lock(mu_);
      sticky_ = {};
      probed_ = {};
      return false;
    }

    float cur = 0.f, mx = sticky.max;
    __try {
      if (sticky.max_delta == 0) {
        cur = sticky.be ? ReadBeFloat(p) : ReadLeFloat(p);
        mx = 100.f;
      } else {
        auto* pm = p + sticky.max_delta;
        if (sticky.as_int) {
          const int32_t ic = sticky.be ? ReadBeInt(p) : ReadLeInt(p);
          const int32_t im = sticky.be ? ReadBeInt(pm) : ReadLeInt(pm);
          cur = static_cast<float>(ic);
          mx = static_cast<float>(im);
        } else {
          cur = sticky.be ? ReadBeFloat(p) : ReadLeFloat(p);
          mx = sticky.be ? ReadBeFloat(pm) : ReadLeFloat(pm);
        }
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      std::lock_guard lock(mu_);
      sticky_ = {};
      probed_ = {};
      return false;
    }
    if (!IsPlausibleHealthPair(cur, mx)) {
      std::lock_guard lock(mu_);
      sticky_ = {};
      probed_ = {};
      return false;
    }
    float display_cur = cur;
    float display_mx = mx;
    if (display_mx > 0.9f && display_mx < 1.1f) {
      display_cur *= 100.f;
      display_mx = 100.f;
    }
    if (sticky.prev_cur >= 0.f && std::fabs(sticky.prev_cur - display_cur) > 0.05f) {
      sticky.changes += 1;
      REXLOG_INFO("HUD: health live change {:.1f}->{:.1f} (n={})", sticky.prev_cur, display_cur,
                  sticky.changes);
    }
    sticky.prev_cur = display_cur;
    sticky.cur = cur;
    sticky.max = mx;
    {
      std::lock_guard lock(mu_);
      sticky_ = sticky;
    }
    PublishGuest(display_cur, display_mx, ScanSyringesNear(p, sticky.be));
    return true;
  }
#endif

  void PublishGuest(float cur, float mx, int syr) {
    HudSnapshot out;
    out.valid = true;
    out.health = cur;
    out.health_max = mx;
    out.syringes = syr;
    out.source = "guest";
    if (syr > 0) {
      out.items[0] = {"Syringe", syr};
      out.item_count = 1;
    }
    std::lock_guard lock(mu_);
    probed_ = out;
  }

  static bool FlagTruthy(const char* name) {
    const std::string v = rex::cvar::GetFlagByName(name);
    return v == "true" || v == "1" || v == "yes" || v == "on";
  }

  struct WorldHints {
    bool level_seen = false;
    bool menu_seen = false;
    bool loading_seen = false;
    bool cutscene_seen = false;
    std::chrono::steady_clock::time_point last_level{};
    std::chrono::steady_clock::time_point last_menu{};
    std::chrono::steady_clock::time_point last_loading{};
    std::chrono::steady_clock::time_point last_cutscene{};
  };

  rex::ui::ImmediateDrawer* immediate_ = nullptr;
  rex::Runtime* runtime_ = nullptr;
  std::chrono::steady_clock::time_point born_{};
  std::thread probe_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> gameplay_ok_{false};
  std::mutex mu_;
  HudSnapshot probed_{};
  HealthCandidate sticky_{};
  WorldHints hints_{};
  std::vector<HealthCandidate> prev_cands_;
  uint64_t scan_window_ = 0;

  bool textures_tried_ = false;
  std::unique_ptr<rex::ui::ImmediateTexture> hp_frame_;
  std::unique_ptr<rex::ui::ImmediateTexture> hp_fill_;
};

}  // namespace

void RegisterCvars() {}

std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer,
                                                    rex::ui::ImmediateDrawer* immediate_drawer,
                                                    rex::Runtime* runtime) {
  if (!drawer) return nullptr;
  return std::unique_ptr<rex::ui::ImGuiDialog>(
      new HudOverlayDialog(drawer, immediate_drawer, runtime));
}

}  // namespace saw2_hud
