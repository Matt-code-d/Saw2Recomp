// Safe host achievement toast for Saw2.
// Text-only: avoids XDBF/icon loads that AV'd this title after XGIUserWriteAchievements.

#pragma once

#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/overlay/achievement_notification.h>

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>

namespace saw2_achievements {

class SafeToastDialog final : public rex::ui::AchievementNotificationDialog {
 public:
  explicit SafeToastDialog(rex::ui::ImGuiDrawer* drawer) : AchievementNotificationDialog(drawer) {
    REXLOG_INFO("Achievements: safe text toast enabled (no XDBF icons)");
  }

  void Push(const rex::system::AchievementEvent& event) override {
    Pending p;
    p.title = event.achievement.label.empty() ? "Achievement unlocked"
                                              : event.achievement.label;
    p.detail = event.achievement.description;
    p.arrived = std::chrono::steady_clock::now();
    std::lock_guard lock(mu_);
    queue_.push_back(std::move(p));
    while (queue_.size() > 6) queue_.pop_front();
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    Pending cur;
    {
      std::lock_guard lock(mu_);
      if (queue_.empty()) return;
      cur = queue_.front();
      const float age =
          std::chrono::duration<float>(std::chrono::steady_clock::now() - cur.arrived).count();
      if (age > kDisplaySeconds) {
        queue_.pop_front();
        if (queue_.empty()) return;
        queue_.front().arrived = std::chrono::steady_clock::now();
        cur = queue_.front();
      }
    }

    const float age =
        std::chrono::duration<float>(std::chrono::steady_clock::now() - cur.arrived).count();
    float alpha = 1.f;
    if (age < 0.25f)
      alpha = age / 0.25f;
    else if (age > kDisplaySeconds - 0.4f)
      alpha = std::clamp((kDisplaySeconds - age) / 0.4f, 0.f, 1.f);

    const float w = 360.f;
    const float h = cur.detail.empty() ? 64.f : 88.f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - w - 24.f, io.DisplaySize.y - h - 28.f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.06f, 0.05f, 0.05f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.55f, 0.08f, 0.1f, 0.85f));

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;

    if (ImGui::Begin("##saw2_ach_toast", nullptr, flags)) {
      ImGui::TextUnformatted("ACHIEVEMENT");
      ImGui::Spacing();
      ImGui::TextWrapped("%s", cur.title.c_str());
      if (!cur.detail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.72f, 0.68f, 1.f));
        ImGui::TextWrapped("%s", cur.detail.c_str());
        ImGui::PopStyleColor();
      }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
  }

 private:
  static constexpr float kDisplaySeconds = 4.5f;

  struct Pending {
    std::string title;
    std::string detail;
    std::chrono::steady_clock::time_point arrived{};
  };

  std::mutex mu_;
  std::deque<Pending> queue_;
};

}  // namespace saw2_achievements
