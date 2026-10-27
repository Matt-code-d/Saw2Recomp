#pragma once

#include <memory>

#include <rex/runtime.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>

namespace saw2_hud {

void RegisterCvars();

// Persistent ImGui HUD (health / syringes / items). Owned by Saw2App.
// Pass ImmediateDrawer so hpbar.png / hpbarline.png can be uploaded as textures.
std::unique_ptr<rex::ui::ImGuiDialog> CreateOverlay(rex::ui::ImGuiDrawer* drawer,
                                                    rex::ui::ImmediateDrawer* immediate_drawer,
                                                    rex::Runtime* runtime);

}  // namespace saw2_hud
