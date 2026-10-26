// saw2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include "ue3_framerate_unlock.h"

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>

#include <string>

class Saw2App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Saw2App>(new Saw2App(ctx, "saw2",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) {
      config.gpu_plugin = "xenos";
    }

    // Soften alpha-tested fog/particle edges. Without this, Xenos often draws
    // hard-edged quads in mist (especially on NVIDIA). Leave alone if the user
    // already passed --use_fuzzy_alpha_epsilon / --no-use_fuzzy_alpha_epsilon.
    if (!rex::cvar::HasNonDefaultValue("use_fuzzy_alpha_epsilon")) {
      rex::cvar::SetFlagByName("use_fuzzy_alpha_epsilon", "true");
    }

    // UE3 puddles: rov + invalid fetch only. Skip readback/gamma/clear_memory —
    // those hitch when scene captures run. Present FSR/CAS/bilinear stay untouched.
    if (!rex::cvar::HasNonDefaultValue("render_target_path_d3d12")) {
      rex::cvar::SetFlagByName("render_target_path_d3d12", "rov");
    }
    if (!rex::cvar::HasNonDefaultValue("gpu_allow_invalid_fetch_constants")) {
      rex::cvar::SetFlagByName("gpu_allow_invalid_fetch_constants", "true");
    }
    if (!rex::cvar::HasNonDefaultValue("async_shader_compilation")) {
      rex::cvar::SetFlagByName("async_shader_compilation", "true");
    }
    if (!rex::cvar::HasNonDefaultValue("texture_cache_memory_limit_render_to_texture")) {
      rex::cvar::SetFlagByName("texture_cache_memory_limit_render_to_texture", "192");
    }
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    (void)drawer;
    ApplyControllerCursorPolicy();
  }

  void OnPreLaunchModule() override { ApplyControllerCursorPolicy(); }

  void OnPostLaunchModule(rex::system::XThread* thread) override {
    (void)thread;
    const float fps = ue3_framerate::TargetFpsFromCvars();
    if (fps <= 0.0f || fps > 60.0f) {
      ue3_framerate::StartBackgroundUnlock(runtime(), fps);
    }
  }

  void OnWindowFocusChanged(bool focused) override {
    if (focused) ApplyControllerCursorPolicy();
  }

  // Built-in toast AV's this title right after XGIUserWriteAchievements
  // (guest then touches missing CookedXenon/<hash>.xxx). Unlocks still persist.
  std::unique_ptr<rex::ui::AchievementNotificationDialog>
  CreateAchievementNotificationDialog() override {
    return nullptr;
  }

 private:
  // Pad-only play: keep the host cursor off the game view. M&K mode leaves
  // visibility alone so the MnK driver can capture / reveal as needed.
  void ApplyControllerCursorPolicy() {
    auto* w = window();
    if (!w) return;
    const std::string mnk = rex::cvar::GetFlagByName("mnk_mode");
    const bool mnk_on = (mnk == "true" || mnk == "1");
    if (!mnk_on) {
      w->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
    }
  }
};
