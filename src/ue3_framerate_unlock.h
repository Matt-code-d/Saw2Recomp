#pragma once

#include <cstdint>
#include <filesystem>

namespace rex {
class Runtime;
}

namespace ue3_framerate {

void EnsureEngineIni(const std::filesystem::path& game_data_root, float target_fps);

uint32_t TryPatchGuestMemory(rex::Runtime* runtime, float target_fps);

// No-op placeholder (old XamInput hook path). Prefer StartBackgroundUnlock.
void MaybePatchFromGuest();

// Delayed VirtualQuery scan for UE3 DefaultProperties Max=62 float slots.
void StartBackgroundUnlock(rex::Runtime* runtime, float target_fps);

float TargetFpsFromCvars();

}  // namespace ue3_framerate
