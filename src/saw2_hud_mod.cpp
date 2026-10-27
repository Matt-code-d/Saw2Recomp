#include "saw2_hud_mod.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
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
  uintptr_t addr = 0;  // current
  int max_delta = 4;
  bool be = true;
  float cur = 0.f;
  float max = 100.f;
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

float ScorePair(float cur, float mx) {
  float score = 5.f;
  if (std::fabs(mx - 100.f) < 0.05f) score += 50.f;
  else if (std::fabs(mx - 1.f) < 0.02f) score += 45.f;
  else if (std::fabs(mx - 50.f) < 0.05f) score += 25.f;
  else if (std::fabs(mx - 200.f) < 0.05f) score += 20.f;
  else if (mx >= 25.f && mx <= 150.f) score += 10.f;
  if (cur > 0.05f) score += 12.f;
  if (cur >= mx * 0.5f) score += 8.f;
  if (cur >= mx * 0.9f) score += 12.f;
  if (std::fabs(cur - mx) < 0.05f * std::max(mx, 1.f)) score += 8.f;
  return score;
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

    HudSnapshot snap = BuildSnapshot();
    if (!snap.valid) {
      snap.valid = true;
      snap.health = snap.health_max;
      snap.source = "pending";
    }

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

  uint8_t* ResolveMembase() {
    if (runtime_) {
      if (uint8_t* b = runtime_->virtual_membase()) return b;
    }
    if (auto* inst = rex::Runtime::instance()) {
      if (uint8_t* b = inst->virtual_membase()) {
        runtime_ = inst;
        return b;
      }
    }
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
    uint8_t* membase = ResolveMembase();
    if (!membase) {
      if ((scan_window_++ % 32) == 0) REXLOG_WARN("HUD: virtual_membase null");
      return;
    }

    // 2 MB windows across typical UE3 heap / physical mirror range.
    constexpr uint64_t kWindow = 0x00200000ull;
    constexpr uint64_t kBegin = 0x20000000ull;
    constexpr uint64_t kEnd = 0x90000000ull;
    const uint64_t nwin = (kEnd - kBegin) / kWindow;
    const uint64_t win0 = kBegin + (scan_window_++ % nwin) * kWindow;
    uint8_t* start = membase + win0;
    uint8_t* end = membase + win0 + kWindow;

    if ((scan_window_ % 16) == 1) {
      REXLOG_INFO("HUD: scan begin 0x{:x} membase={}", win0, static_cast<const void*>(membase));
    }

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
    }

    static const uint8_t kBe100[] = {0x42, 0xC8, 0x00, 0x00};
    static const uint8_t kLe100[] = {0x00, 0x00, 0xC8, 0x42};
    static const uint8_t kBe50[] = {0x42, 0x48, 0x00, 0x00};
    static const uint8_t kLe50[] = {0x00, 0x00, 0x48, 0x42};
    // Normalized HealthMax=1.0 (common for percent-style pawns / RTPC mirrors).
    static const uint8_t kBe1[] = {0x3F, 0x80, 0x00, 0x00};
    static const uint8_t kLe1[] = {0x00, 0x00, 0x80, 0x3F};

    HealthCandidate best{};
    float best_score = -1.f;
    int regions = 0;
    int hits = 0;
    int str_hits = 0;
    std::vector<uint8_t> page(0x1000);

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

      if (RegionWritable(mbi) || RegionReadable(mbi)) {
        uint8_t* from = region < start ? start : region;
        uint8_t* to = region_end > end ? end : region_end;
        if (to > from + 16) {
          ++regions;
          for (uint8_t* page_ptr = from; page_ptr < to && !stop_.load(); page_ptr += 0x1000) {
            const size_t n = std::min<size_t>(0x1000, static_cast<size_t>(to - page_ptr));
            if (!SafeCopy(page.data(), page_ptr, n)) continue;

            str_hits += ScanMarkersInBuffer(page.data(), n, page_ptr);

            if (!RegionWritable(mbi) || sticky.addr) continue;

            auto consider_pat = [&](const uint8_t* pat, float maxv, bool be) {
              const uint8_t* cur = page.data();
              const uint8_t* lim = page.data() + n;
              int local = 0;
              while (cur + 4 <= lim && local < 64) {
                const uint8_t* hit = FindBytes(cur, static_cast<size_t>(lim - cur), pat, 4);
                if (!hit) break;
                ++hits;
                ++local;
                const ptrdiff_t off = hit - page.data();
                ConsiderAnchor(page_ptr + off, maxv, be, best, best_score);
                cur = hit + 4;
              }
            };
            consider_pat(kBe100, 100.f, true);
            consider_pat(kLe100, 100.f, false);
            consider_pat(kBe50, 50.f, true);
            consider_pat(kLe50, 50.f, false);
            consider_pat(kBe1, 1.f, true);
            consider_pat(kLe1, 1.f, false);

            // Wwise RTPC name in this page.
            const uint8_t* ph = FindBytes(page.data(), n,
                                          reinterpret_cast<const uint8_t*>("PlayerHealth"), 12);
            if (ph) {
              ScanPlayerHealthRtpc(page_ptr + (ph - page.data()) - 128,
                                   page_ptr + (ph - page.data()) + 140, best, best_score);
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
    REXLOG_INFO("HUD: scan end 0x{:x} regions={} hits={} str={} best={:.1f} level={}", win0,
                regions, hits, str_hits, best_score, level);

    // Normalize percent-style pairs to 0..100 for the HUD fill.
    auto publish_scaled = [&](HealthCandidate c, float score) {
      float cur = c.cur;
      float mx = c.max;
      if (mx > 0.9f && mx < 1.1f) {
        cur *= 100.f;
        mx = 100.f;
      }
      const int syr = ScanSyringesNear(reinterpret_cast<uint8_t*>(c.addr), c.be);
      {
        std::lock_guard lock(mu_);
        sticky_ = c;
      }
      PublishGuest(cur, mx, syr);
      REXLOG_INFO("HUD: BOUND health {:.1f}/{:.1f} ({}) score={:.0f} syr={}", cur, mx,
                  c.be ? "BE" : "LE", score, syr);
    };

    if (best_score >= 28.f && best.addr && !sticky.addr) {
      bool confirmed = false;
      for (const auto& prev : prev_cands_) {
        if (prev.addr == best.addr && prev.be == best.be &&
            std::fabs(prev.max - best.max) < 0.1f) {
          confirmed = true;
          break;
        }
      }
      prev_cands_.clear();
      prev_cands_.push_back(best);

      if (confirmed || best_score >= 48.f) {
        publish_scaled(best, best_score);
      } else {
        REXLOG_INFO("HUD: candidate {:.1f}/{:.1f} score={:.0f} — confirm", best.cur, best.max,
                    best_score);
      }
    }
#else
    (void)runtime_;
#endif
  }

#if defined(_WIN32)
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

  // `from`..`to` is a small guest window around a "PlayerHealth" string hit.
  void ScanPlayerHealthRtpc(uint8_t* from, uint8_t* to, HealthCandidate& best, float& best_score) {
    if (!from || to <= from + 8) return;
    std::vector<uint8_t> tmp(static_cast<size_t>(to - from));
    if (!SafeCopy(tmp.data(), from, tmp.size())) return;

    for (size_t i = 0; i + 8 <= tmp.size(); i += 4) {
      float a = 0.f, b = 0.f;
      std::memcpy(&a, tmp.data() + i, 4);
      std::memcpy(&b, tmp.data() + i + 4, 4);
      // LE pair
      if (IsPlausibleHealthPair(a, b) && std::fabs(b - 100.f) < 0.1f) {
        const float score = ScorePair(a, b) + 40.f;
        if (score > best_score) {
          best_score = score;
          best = {reinterpret_cast<uintptr_t>(from + i), 4, false, a, b};
        }
      }
      // Solo LE float as RTPC
      if (FiniteOk(a) && a > 1.f && a <= 100.f) {
        const float score = 28.f + (a / 100.f) * 10.f;
        if (score > best_score) {
          best_score = score;
          best = {reinterpret_cast<uintptr_t>(from + i), 0, false, a, 100.f};
        }
      }
      // BE pair
      a = ReadBeFloat(tmp.data() + i);
      b = ReadBeFloat(tmp.data() + i + 4);
      if (IsPlausibleHealthPair(a, b) && std::fabs(b - 100.f) < 0.1f) {
        const float score = ScorePair(a, b) + 40.f;
        if (score > best_score) {
          best_score = score;
          best = {reinterpret_cast<uintptr_t>(from + i), 4, true, a, b};
        }
      }
    }
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

    if (IsPlausibleHealthPair(before, mx)) {
      float score = ScorePair(before, mx);
      if (score > best_score) {
        best_score = score;
        best = {reinterpret_cast<uintptr_t>(max_ptr - 4), 4, be, before, mx};
      }
    }
    if (IsPlausibleHealthPair(after, mx)) {
      float score = ScorePair(after, mx) - 1.f;
      if (score > best_score) {
        best_score = score;
        best = {reinterpret_cast<uintptr_t>(max_ptr + 4), -4, be, after, mx};
      }
    }
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
    auto take = [&](int32_t cur, int delta, float bias) {
      if (cur < 0 || cur > mx) return;
      const float fcur = static_cast<float>(cur);
      const float fmx = static_cast<float>(mx);
      float score = ScorePair(fcur, fmx) + bias;
      if (score > best_score) {
        best_score = score;
        best = {reinterpret_cast<uintptr_t>(max_ptr + delta), -delta, be, fcur, fmx};
        // max_delta: store as int path via be flag + special delta; reread as float won't work.
        // Reuse float reader only if we convert sticky to int mode — mark with max_delta odd.
        best.max_delta = (delta == -4) ? 4 : -4;
      }
    };
    take(before, -4, 5.f);
    take(after, 4, 4.f);
  }

  bool RereadSticky(const HealthCandidate& sticky) {
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
        // Solo RTPC float.
        cur = sticky.be ? ReadBeFloat(p) : ReadLeFloat(p);
        mx = 100.f;
      } else {
        auto* pm = p + sticky.max_delta;
        cur = sticky.be ? ReadBeFloat(p) : ReadLeFloat(p);
        mx = sticky.be ? ReadBeFloat(pm) : ReadLeFloat(pm);
        // If floats look garbage, try ints (int-bound candidates).
        if (!IsPlausibleHealthPair(cur, mx)) {
          const int32_t ic = sticky.be ? ReadBeInt(p) : ReadLeInt(p);
          const int32_t im = sticky.be ? ReadBeInt(pm) : ReadLeInt(pm);
          if (im >= 10 && im <= 1000 && ic >= 0 && ic <= im) {
            cur = static_cast<float>(ic);
            mx = static_cast<float>(im);
          }
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
    if (mx > 0.9f && mx < 1.1f) {
      cur *= 100.f;
      mx = 100.f;
    }
    PublishGuest(cur, mx, ScanSyringesNear(p, sticky.be));
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
