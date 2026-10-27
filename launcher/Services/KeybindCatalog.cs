namespace Saw2Launcher.Services;

/// <summary>Keyboard → Xbox 360 pad mapping (ReXGlue keybind_* cvars, used with M&amp;K mode).</summary>
public static class KeybindCatalog
{
    public sealed record Entry(string Cvar, string LabelKey, string GroupKey, string DefaultKeys);

    // Defaults match ReXGlue MNK where known; otherwise a common WASD-style layout.
    public static readonly Entry[] All =
    [
        new("keybind_lstick_up", "input.bind.lstick_up", "input.group.move", "W"),
        new("keybind_lstick_down", "input.bind.lstick_down", "input.group.move", "S"),
        new("keybind_lstick_left", "input.bind.lstick_left", "input.group.move", "A"),
        new("keybind_lstick_right", "input.bind.lstick_right", "input.group.move", "D"),
        new("keybind_lstick_press", "input.bind.lstick_press", "input.group.move", "LeftCtrl"),

        new("keybind_rstick_up", "input.bind.rstick_up", "input.group.look", "Up"),
        new("keybind_rstick_down", "input.bind.rstick_down", "input.group.look", "Down"),
        new("keybind_rstick_left", "input.bind.rstick_left", "input.group.look", "Left"),
        new("keybind_rstick_right", "input.bind.rstick_right", "input.group.look", "Right"),
        new("keybind_rstick_press", "input.bind.rstick_press", "input.group.look", "RightCtrl"),

        new("keybind_a", "input.bind.a", "input.group.face", "Space"),
        new("keybind_b", "input.bind.b", "input.group.face", "Backspace"),
        new("keybind_x", "input.bind.x", "input.group.face", "X"),
        new("keybind_y", "input.bind.y", "input.group.face", "C"),

        new("keybind_left_trigger", "input.bind.lt", "input.group.shoulders", "Q"),
        new("keybind_right_trigger", "input.bind.rt", "input.group.shoulders", "E"),
        new("keybind_left_shoulder", "input.bind.lb", "input.group.shoulders", "1"),
        new("keybind_right_shoulder", "input.bind.rb", "input.group.shoulders", "3"),

        new("keybind_dpad_up", "input.bind.dpad_up", "input.group.dpad", "T"),
        new("keybind_dpad_down", "input.bind.dpad_down", "input.group.dpad", "G"),
        new("keybind_dpad_left", "input.bind.dpad_left", "input.group.dpad", "F"),
        new("keybind_dpad_right", "input.bind.dpad_right", "input.group.dpad", "H"),

        new("keybind_back", "input.bind.back", "input.group.system", "Tab"),
        new("keybind_start", "input.bind.start", "input.group.system", "Enter"),
        new("keybind_guide", "input.bind.guide", "input.group.system", "F1"),
    ];

    public static IEnumerable<IGrouping<string, Entry>> ByGroup() =>
        All.GroupBy(e => e.GroupKey);

    public static Dictionary<string, string> DefaultMap() =>
        All.ToDictionary(e => e.Cvar, e => e.DefaultKeys, StringComparer.OrdinalIgnoreCase);
}
