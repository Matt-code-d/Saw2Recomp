#include "ue3_framerate_unlock.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ue3_framerate {
namespace {

std::atomic<bool> g_started{false};
std::atomic<bool> g_done{false};
std::atomic<uint32_t> g_patches{0};

// UE3 DefaultProperties float slot: Size=4, ArrayIndex=0, Value=62.0f (BE).
// Unique in decompressed Engine.xxx (one hit). Safer than Min=22/Max=62 pair scans.
constexpr uint8_t kProp62[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
                               0x42, 0x78, 0x00, 0x00};
constexpr uint8_t kProp60[] = {0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
                               0x42, 0x70, 0x00, 0x00};

void WriteBeFloat(uint8_t* dst, float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = __builtin_bswap32(bits);
  std::memcpy(dst, &bits, sizeof(bits));
}

uint32_t PatchBuffer(uint8_t* data, size_t size, float max_out) {
  if (!data || size < sizeof(kProp62)) return 0;
  uint32_t n = 0;
  // Fast path: probe for BE float 62/60, then confirm full property slot header.
  for (size_t i = 0; i + sizeof(kProp62) <= size; ++i) {
    const uint8_t b0 = data[i + 8];
    if (b0 != 0x42) continue;
    const uint8_t b1 = data[i + 9];
    if (b1 != 0x78 && b1 != 0x70) continue;
    if (std::memcmp(data + i, kProp62, 8) != 0 && std::memcmp(data + i, kProp60, 8) != 0)
      continue;
    if (std::memcmp(data + i + 8, kProp62 + 8, 4) != 0 &&
        std::memcmp(data + i + 8, kProp60 + 8, 4) != 0)
      continue;
    WriteBeFloat(data + i + 8, max_out);
    ++n;
    i += 11;
  }
  return n;
}

#if defined(_WIN32)
// Walk host pages under guest virtual_membase with VirtualQuery only — do not
// call rex::memory::LookupHeap (that AV'd this title previously).
uint32_t PatchViaVirtualQuery(uint8_t* membase, float max_out) {
  if (!membase) return 0;

  SYSTEM_INFO si{};
  GetSystemInfo(&si);

  // Typical title heaps + physical mirrors live in this guest VA window.
  uint8_t* start = membase + 0x10000000ull;
  uint8_t* end = membase + 0x90000000ull;
  if (end <= start) return 0;

  uint32_t patched = 0;
  uint8_t* p = start;
  while (p < end) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) break;

    uint8_t* region = static_cast<uint8_t*>(mbi.BaseAddress);
    uint8_t* region_end = region + mbi.RegionSize;
    if (region_end <= start) {
      p = region_end;
      continue;
    }
    if (region >= end) break;

    const bool committed = (mbi.State == MEM_COMMIT);
    const DWORD prot = mbi.Protect & 0xFF;
    const bool readable =
        prot == PAGE_READONLY || prot == PAGE_READWRITE || prot == PAGE_WRITECOPY ||
        prot == PAGE_EXECUTE_READ || prot == PAGE_EXECUTE_READWRITE ||
        prot == PAGE_EXECUTE_WRITECOPY;

    if (committed && readable && !(mbi.Protect & PAGE_GUARD)) {
      uint8_t* from = region < start ? start : region;
      uint8_t* to = region_end > end ? end : region_end;
      if (to > from) {
        patched += PatchBuffer(from, static_cast<size_t>(to - from), max_out);
      }
    }

    if (region_end <= p) {
      p += si.dwPageSize;
    } else {
      p = region_end;
    }
  }
  return patched;
}
#else
uint32_t PatchViaVirtualQuery(uint8_t*, float) { return 0; }
#endif

}  // namespace

float TargetFpsFromCvars() {
  const std::string s = rex::cvar::GetFlagByName("video_mode_refresh_rate");
  if (s.empty()) return 0.0f;
  try {
    return std::stof(s);
  } catch (...) {
    return 0.0f;
  }
}

void EnsureEngineIni(const std::filesystem::path& game_data_root, float target_fps) {
  (void)game_data_root;
  (void)target_fps;
}

uint32_t TryPatchGuestMemory(rex::Runtime* runtime, float target_fps) {
  if (!runtime) return 0;
  uint8_t* base = runtime->virtual_membase();
  if (!base) return 0;
  const float max_out = (target_fps <= 0.0f) ? 1000.0f : target_fps;
  const uint32_t n = PatchViaVirtualQuery(base, max_out);
  if (n) {
    g_patches.fetch_add(n);
    REXLOG_INFO("UE3 SmoothFrameRate unlock: patched {} Max property slot(s) -> {:.0f}", n,
                max_out);
  }
  return n;
}

void MaybePatchFromGuest() {
  // Prefer the delayed VirtualQuery path started from the app; no-op here.
}

void StartBackgroundUnlock(rex::Runtime* runtime, float target_fps) {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) return;
  if (!runtime) return;

  if (target_fps > 0.0f && target_fps <= 60.0f) {
    g_done.store(true);
    return;
  }

  std::thread([runtime, target_fps] {
    // Engine.xxx finishes decompressing well into boot; wait for CDO defaults.
    std::this_thread::sleep_for(std::chrono::seconds(30));
    for (int attempt = 0; attempt < 20 && !g_done.load(); ++attempt) {
      const uint32_t n = TryPatchGuestMemory(runtime, target_fps);
      if (n > 0 || g_patches.load() > 0) {
        // Re-apply a few times in case the CDO is recopied.
        for (int again = 0; again < 5; ++again) {
          std::this_thread::sleep_for(std::chrono::seconds(4));
          TryPatchGuestMemory(runtime, target_fps);
        }
        g_done.store(true);
        return;
      }
      std::this_thread::sleep_for(std::chrono::seconds(3));
    }
    if (!g_patches.load()) {
      REXLOG_WARN("UE3 SmoothFrameRate unlock: property Max=62 slot not found");
    }
    g_done.store(true);
  }).detach();
}

}  // namespace ue3_framerate
