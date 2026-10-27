// saw2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include "saw2_achievement_toast.h"
#include "saw2_hud_mod.h"
#include "ue3_framerate_unlock.h"

#include <rex/cvar.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/xthread.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>

#include <memory>
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

    if (!rex::cvar::HasNonDefaultValue("use_fuzzy_alpha_epsilon")) {
      rex::cvar::SetFlagByName("use_fuzzy_alpha_epsilon", "true");
    }
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
    saw2_hud::RegisterCvars();
    hud_overlay_ = saw2_hud::CreateOverlay(drawer, immediate_drawer(), runtime());
    // Own the toast ourselves (same pattern as HUD). Do NOT return it from
    // CreateAchievementNotificationDialog — that path null-deref'd on this title.
    ach_toast_.reset();
    if (AchievementsEnabled() && drawer) {
      ach_toast_ = std::unique_ptr<saw2_achievements::SafeToastDialog>(
          new saw2_achievements::SafeToastDialog(drawer));
    }
    ApplyControllerCursorPolicy();
  }

  void OnPostSetup() override { ApplyAchievementsPolicy(); }

  void OnPreLaunchModule() override {
    ApplyControllerCursorPolicy();
    ApplyAchievementsPolicy();
  }

  void OnPostLaunchModule(rex::system::XThread* thread) override {
    (void)thread;
    ApplyAchievementsPolicy();
    HookAchievementToast();
    const float fps = ue3_framerate::TargetFpsFromCvars();
    if (fps <= 0.0f || fps > 60.0f) {
      ue3_framerate::StartBackgroundUnlock(runtime(), fps);
    }
  }

  void OnWindowFocusChanged(bool focused) override {
    if (focused) ApplyControllerCursorPolicy();
  }

  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    return nullptr;
  }

  std::unique_ptr<rex::ui::AchievementNotificationDialog>
  CreateAchievementNotificationDialog() override {
    return nullptr;
  }

 private:
  static bool FlagTruthy(const std::string& v) {
    return v == "true" || v == "1" || v == "yes" || v == "on";
  }

  static bool AchievementsEnabled() {
    const std::string v = rex::cvar::GetFlagByName("saw2_achievements");
    if (v.empty()) return true;
    return FlagTruthy(v);
  }

  void ApplyAchievementsPolicy() {
    try {
      if (!AchievementsEnabled()) {
        achievements().ReplaceAchievements({});
        ach_toast_.reset();
      }
    } catch (...) {
    }
  }

  void HookAchievementToast() {
    if (ach_toast_hooked_ || !ach_toast_ || !AchievementsEnabled()) return;
    ach_toast_hooked_ = true;
    achievements().RegisterUnlockCallback([this](const rex::system::AchievementEvent& ev) {
      if (ach_toast_) ach_toast_->Push(ev);
    });
  }

  void ApplyControllerCursorPolicy() {
    auto* w = window();
    if (!w) return;
    const std::string mnk = rex::cvar::GetFlagByName("mnk_mode");
    if (!FlagTruthy(mnk)) {
      w->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
    }
  }

  std::unique_ptr<rex::ui::ImGuiDialog> hud_overlay_;
  std::unique_ptr<saw2_achievements::SafeToastDialog> ach_toast_;
  bool ach_toast_hooked_ = false;
};
