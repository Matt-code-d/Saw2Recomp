namespace Saw2Launcher.Services;

/// <summary>Keyboard → Xbox 360 pad mapping (ReXGlue keybind_* cvars, used with M&amp;K mode).</summary>
public static class KeybindCatalog
{
    public sealed record Entry(string Cvar, string LabelKey, string DefaultKeys);

    // Defaults match ReXGlue MNK where known; otherwise a common WASD-style layout.
    public static readonly Entry[] All =
    [
        new("keybind_lstick_up", "input.bind.lstick_up", "W"),
        new("keybind_lstick_down", "input.bind.lstick_down", "S"),
        new("keybind_lstick_left", "input.bind.lstick_left", "A"),
        new("keybind_lstick_right", "input.bind.lstick_right", "D"),
        new("keybind_lstick_press", "input.bind.lstick_press", "LeftCtrl"),
        new("keybind_rstick_up", "input.bind.rstick_up", "Up"),
        new("keybind_rstick_down", "input.bind.rstick_down", "Down"),
        new("keybind_rstick_left", "input.bind.rstick_left", "Left"),
        new("keybind_rstick_right", "input.bind.rstick_right", "Right"),
        new("keybind_rstick_press", "input.bind.rstick_press", "RightCtrl"),
        new("keybind_a", "input.bind.a", "Space"),
        new("keybind_b", "input.bind.b", "Backspace"),
        new("keybind_x", "input.bind.x", "X"),
        new("keybind_y", "input.bind.y", "C"),
        new("keybind_left_trigger", "input.bind.lt", "Q"),
        new("keybind_right_trigger", "input.bind.rt", "E"),
        new("keybind_left_shoulder", "input.bind.lb", "1"),
        new("keybind_right_shoulder", "input.bind.rb", "3"),
        new("keybind_dpad_up", "input.bind.dpad_up", "T"),
        new("keybind_dpad_down", "input.bind.dpad_down", "G"),
        new("keybind_dpad_left", "input.bind.dpad_left", "F"),
        new("keybind_dpad_right", "input.bind.dpad_right", "H"),
        new("keybind_back", "input.bind.back", "Tab"),
        new("keybind_start", "input.bind.start", "Enter"),
        new("keybind_guide", "input.bind.guide", "F1"),
    ];

    public static Dictionary<string, string> DefaultMap() =>
        All.ToDictionary(e => e.Cvar, e => e.DefaultKeys, StringComparer.OrdinalIgnoreCase);
}
