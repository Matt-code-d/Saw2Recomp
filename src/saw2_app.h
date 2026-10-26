// saw2 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>
#include <rex/runtime.h>

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
  }

  // Built-in toast AV's this title right after XGIUserWriteAchievements
  // (guest then touches missing CookedXenon/<hash>.xxx). Unlocks still persist.
  std::unique_ptr<rex::ui::AchievementNotificationDialog>
  CreateAchievementNotificationDialog() override {
    return nullptr;
  }
};