// game_fixes.h - guest-function overrides
//
// Included once from src/main.cpp after the generated init header. Strong
// extern "C" symbols replace the weak DEFINE_REX_FUNC aliases for the same
// names; __imp__* remains the original recompiled body.
#pragma once

#include <atomic>
#include <mutex>

#include <rex/hook.h>
#include <rex/logging.h>

namespace game_fixes {

// rex::input::InputSystem has no lock: GetState / GetCapabilities / SetState
// each re-enumerate devices and rebuild devices_ (heap strings). Parallel
// guest XamInput* calls with a pad connected can double-free -> 0xC0000374.
// Same race fixed in ArmyOfTwoRecomp; Saw II hits it mid-session.
inline std::mutex g_input_mutex;
inline std::atomic<uint32_t> g_input_contention{0};

class InputLock {
 public:
  InputLock() {
    if (!g_input_mutex.try_lock()) {
      if (g_input_contention.fetch_add(1, std::memory_order_relaxed) == 0) {
        REXLOG_WARN(
            "[fix] XamInput called from two guest threads; serialized "
            "(logged once)");
      }
      g_input_mutex.lock();
    }
  }
  ~InputLock() { g_input_mutex.unlock(); }
  InputLock(const InputLock&) = delete;
  InputLock& operator=(const InputLock&) = delete;
};

}  // namespace game_fixes

// XInputGetCapabilities -> XamInputGetCapabilities
REX_HOOK_RAW(sub_82BA3980) {
  game_fixes::InputLock lock;
  __imp__sub_82BA3980(ctx, base);
}

// XInputGetState -> XamInputGetState
REX_HOOK_RAW(sub_82BA3988) {
  game_fixes::InputLock lock;
  __imp__sub_82BA3988(ctx, base);
}

// XInputSetState (rumble) -> GetCapabilities + SetState
REX_HOOK_RAW(sub_82BA3998) {
  game_fixes::InputLock lock;
  __imp__sub_82BA3998(ctx, base);
}
