using System.Text.Json;
using System.Text.Json.Serialization;
using Saw2Launcher.Services;

namespace Saw2Launcher.Models;

public sealed class LauncherSettings
{
    public string InstallRoot { get; set; } = "";
    public string GameDataRoot { get; set; } = "";
    public string GameExePath { get; set; } = "";

    public uint UserLanguage { get; set; } = 1;
    public string UiLocale { get; set; } = "en";

    // Video / display
    public string Resolution { get; set; } = "1080p";
    public int WindowWidth { get; set; } = 1920;
    public int WindowHeight { get; set; } = 1080;
    public bool Fullscreen { get; set; } = true;
    public int Monitor { get; set; } = 0;
    public double RefreshRate { get; set; } = 60;
    public string FpsPreset { get; set; } = "60";
    public bool VSync { get; set; } = true;
    public bool AllowTearing { get; set; } = false;
    public bool ClockNoScaling { get; set; } = false;
    public bool PresentLetterbox { get; set; } = true;
    public bool PresentDither { get; set; } = false;
    public string GpuPlugin { get; set; } = "xenos";
    public string PresentEffect { get; set; } = "bilinear";
    public string FsrQualityMode { get; set; } = "auto";
    public double FsrSharpnessReduction { get; set; } = 0.2;
    public int FsrMaxUpsamplingPasses { get; set; } = 4;
    public double CasAdditionalSharpness { get; set; } = 0.0;

    // Audio
    public bool AudioMute { get; set; } = false;
    public int AudioMaxQFrames { get; set; } = 8;

    // Input
    public string InputBackend { get; set; } = "sdl";
    public bool MnkMode { get; set; } = false;
    public bool MnkMouse { get; set; } = true;
    // Mouse→stick only; real controllers ignore this.
    public double MnkSensitivity { get; set; } = 3.0;
    public bool GuideButton { get; set; } = false;
    /// <summary>ReXGlue keybind_* → key name(s). Empty map uses catalog defaults when M&amp;K is on.</summary>
    public Dictionary<string, string> Keybinds { get; set; } = new(StringComparer.OrdinalIgnoreCase);

    // Advanced
    public bool SkipBootLogos { get; set; } = false;
    public string LogLevel { get; set; } = "info";
    public bool VerboseLog { get; set; } = false;
    public bool WriteLogFile { get; set; } = true;
    public int UserCountry { get; set; } = 103;
    public string ExtraArgs { get; set; } = "";

    public static string DefaultPath()
    {
        var dir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
            "saw2");
        Directory.CreateDirectory(dir);
        return Path.Combine(dir, "launcher_settings.json");
    }

    public static LauncherSettings Load(string? path = null)
    {
        path ??= DefaultPath();
        if (!File.Exists(path))
            return new LauncherSettings();
        try
        {
            var json = File.ReadAllText(path);
            return JsonSerializer.Deserialize<LauncherSettings>(json, JsonOpts()) ?? new LauncherSettings();
        }
        catch
        {
            return new LauncherSettings();
        }
    }

    public void Save(string? path = null)
    {
        path ??= DefaultPath();
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, JsonSerializer.Serialize(this, JsonOpts()));
    }

    public List<string> ToProcessArguments()
    {
        var args = new List<string>();
        if (!string.IsNullOrWhiteSpace(GameDataRoot))
            args.Add($"--game_data_root={GameDataRoot}");

        args.Add($"--gpu_plugin={GpuPlugin}");
        args.Add($"--user_language={UserLanguage}");
        args.Add($"--user_country={UserCountry}");
        args.Add($"--resolution={Resolution}");
        args.Add($"--window_width={WindowWidth}");
        args.Add($"--window_height={WindowHeight}");
        args.Add(Fullscreen ? "--fullscreen" : "--no-fullscreen");
        args.Add($"--monitor={Monitor}");
        args.Add($"--video_mode_refresh_rate={RefreshRate}");
        args.Add(VSync ? "--vsync" : "--no-vsync");
        args.Add(AllowTearing
            ? "--d3d12_allow_variable_refresh_rate_and_tearing"
            : "--no-d3d12_allow_variable_refresh_rate_and_tearing");
        args.Add(ClockNoScaling ? "--clock_no_scaling" : "--no-clock_no_scaling");
        args.Add(PresentLetterbox ? "--present_letterbox" : "--no-present_letterbox");
        args.Add(PresentDither ? "--present_dither" : "--no-present_dither");
        args.Add($"--present_effect={PresentEffect}");
        // Soft alpha test for fog/particle quads (Xenos common square artifact).
        // Override with ExtraArgs: --no-use_fuzzy_alpha_epsilon
        args.Add("--use_fuzzy_alpha_epsilon");

        // Puddle/reflection fix — minimal set. rov is what actually clears the black
        // squares; readback/gamma/clear_memory hitch near water and are omitted.
        // Present upscaling (FSR/CAS/bilinear) is host-side and independent of this.
        args.Add("--render_target_path_d3d12=rov");
        args.Add("--gpu_allow_invalid_fetch_constants");
        args.Add("--resolution_scale=1");
        args.Add("--async_shader_compilation");
        // Keep UE3 scene-capture RTTs resident (MB). Cuts thrash when looking at water.
        args.Add("--texture_cache_memory_limit_render_to_texture=192");

        var effect = PresentEffect.Trim().ToLowerInvariant();
        if (effect is "fsr" or "fsr2" or "fsr3")
        {
            args.Add($"--present_fsr_quality_mode={FsrQualityMode}");
            args.Add($"--present_fsr_sharpness_reduction={FsrSharpnessReduction}");
            args.Add($"--present_fsr_max_upsampling_passes={FsrMaxUpsamplingPasses}");
        }
        if (effect is "cas" or "fsr" or "fsr2" or "fsr3")
            args.Add($"--present_cas_additional_sharpness={CasAdditionalSharpness}");

        args.Add(AudioMute ? "--audio_mute" : "--no-audio_mute");
        args.Add($"--audio_maxqframes={AudioMaxQFrames}");
        args.Add($"--input_backend={InputBackend}");
        args.Add(MnkMode ? "--mnk_mode" : "--no-mnk_mode");
        args.Add(MnkMouse ? "--mnk_mouse" : "--no-mnk_mouse");
        // UI value is a comfort scale; engine still maps through stick emulation.
        args.Add($"--mnk_sensitivity={MnkSensitivity.ToString(System.Globalization.CultureInfo.InvariantCulture)}");
        args.Add(GuideButton ? "--guide_button" : "--no-guide_button");
        if (MnkMode)
        {
            var map = KeybindCatalog.DefaultMap();
            foreach (var kv in Keybinds)
            {
                if (!string.IsNullOrWhiteSpace(kv.Value))
                    map[kv.Key] = kv.Value.Trim();
            }
            // Mouse owns look (right stick). Don't also inject keyboard rstick binds —
            // they fight the mouse and feel like "camera still on arrows".
            if (MnkMouse)
            {
                foreach (var k in map.Keys.Where(k =>
                             k.StartsWith("keybind_rstick_", StringComparison.OrdinalIgnoreCase)).ToList())
                    map.Remove(k);
            }
            foreach (var kv in map)
                args.Add($"--{kv.Key}={kv.Value}");
        }
        args.Add($"--log_level={LogLevel}");
        if (VerboseLog) args.Add("--verbose");
        if (WriteLogFile)
        {
            var log = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                "saw2", "saw2.log");
            args.Add($"--log_file={log}");
        }

        if (!string.IsNullOrWhiteSpace(ExtraArgs))
            args.AddRange(SplitArgs(ExtraArgs));

        return args;
    }

    private static IEnumerable<string> SplitArgs(string raw)
    {
        var list = new List<string>();
        var cur = "";
        var inQuote = false;
        foreach (var ch in raw)
        {
            if (ch == '"') { inQuote = !inQuote; continue; }
            if (char.IsWhiteSpace(ch) && !inQuote)
            {
                if (cur.Length > 0) { list.Add(cur); cur = ""; }
                continue;
            }
            cur += ch;
        }
        if (cur.Length > 0) list.Add(cur);
        return list;
    }

    private static JsonSerializerOptions JsonOpts() => new()
    {
        WriteIndented = true,
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };
}

public sealed class DetectedLanguage
{
    public uint Id { get; init; }
    public string Code { get; init; } = "";
    public string DisplayName { get; init; } = "";
    public string StartupToken { get; init; } = "";
}