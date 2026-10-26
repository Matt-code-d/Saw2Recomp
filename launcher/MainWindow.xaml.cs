using System.Diagnostics;
using System.Globalization;
using System.Windows;
using System.Windows.Controls;
using Microsoft.Win32;
using Saw2Launcher.Models;
using Saw2Launcher.Services;

namespace Saw2Launcher;

public partial class MainWindow : Window
{
    private readonly string _projectRoot;
    private LauncherSettings _settings;
    private CancellationTokenSource? _installCts;
    private readonly Dictionary<string, FrameworkElement> _panels;
    private readonly Dictionary<string, Button> _nav;
    private bool _uiReady;
    private LauncherLocale _l = LauncherLocale.En;
    private string _activePanel = "PLAY";
    private readonly Dictionary<string, TextBox> _keybindBoxes = new(StringComparer.OrdinalIgnoreCase);
    private readonly Dictionary<string, TextBlock> _keybindLabels = new(StringComparer.OrdinalIgnoreCase);

    public MainWindow()
    {
        InitializeComponent();
        _projectRoot = Paths.FindProjectRoot();
        _settings = LauncherSettings.Load();

        _panels = new Dictionary<string, FrameworkElement>
        {
            ["PLAY"] = PanelPlay,
            ["SETUP"] = PanelSetup,
            ["VIDEO"] = PanelVideo,
            ["AUDIO"] = PanelAudio,
            ["INPUT"] = PanelInput,
            ["LANGUAGE"] = PanelLanguage,
            ["ADVANCED"] = PanelAdvanced,
        };
        _nav = new Dictionary<string, Button>
        {
            ["PLAY"] = NavPlay,
            ["SETUP"] = NavSetup,
            ["VIDEO"] = NavVideo,
            ["AUDIO"] = NavAudio,
            ["INPUT"] = NavInput,
            ["LANGUAGE"] = NavLanguage,
            ["ADVANCED"] = NavAdvanced,
        };

        SldAudioQ.ValueChanged += (_, _) => RefreshAudioFramesLabel();
        SldSens.ValueChanged += (_, _) => TxtSens.Text = SldSens.Value.ToString("0.0");
        SldFsrSharp.ValueChanged += (_, _) => TxtFsrSharp.Text = SldFsrSharp.Value.ToString("0.00");
        SldCasSharp.ValueChanged += (_, _) => TxtCasSharp.Text = SldCasSharp.Value.ToString("0.00");

        BuildKeybindUi();
        ApplyDefaultsIfEmpty();
        try { BootLogoSkip.Restore(_settings.GameDataRoot); } catch { /* ignore */ }
        LoadUiFromSettings();
        RefreshInstallStatus();
        RescanLanguages(preferOs: _settings.UserLanguage == 0 || _settings.UserLanguage == 1);
        ApplyLocaleFromSettings();
        TxtSens.Text = SldSens.Value.ToString("0.0");
        TxtFsrSharp.Text = SldFsrSharp.Value.ToString("0.00");
        TxtCasSharp.Text = SldCasSharp.Value.ToString("0.00");
        UpdateFpsUi();
        UpdateUpscaleUi();
        UpdateKeybindPanelEnabled();
        ShowPanel("PLAY");
        _uiReady = true;
    }

    private void BuildKeybindUi()
    {
        PanelKeybinds.Children.Clear();
        _keybindBoxes.Clear();
        _keybindLabels.Clear();
        foreach (var entry in KeybindCatalog.All)
        {
            var row = new Grid { Margin = new Thickness(0, 0, 0, 8) };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(220) });
            var label = new TextBlock
            {
                Style = (Style)FindResource("FieldLabel"),
                VerticalAlignment = VerticalAlignment.Center,
                Text = entry.Cvar,
            };
            var box = new TextBox { Text = entry.DefaultKeys, Tag = entry.Cvar };
            box.PreviewKeyDown += KeybindBox_PreviewKeyDown;
            Grid.SetColumn(label, 0);
            Grid.SetColumn(box, 1);
            row.Children.Add(label);
            row.Children.Add(box);
            PanelKeybinds.Children.Add(row);
            _keybindLabels[entry.Cvar] = label;
            _keybindBoxes[entry.Cvar] = box;
        }
    }

    private void KeybindBox_PreviewKeyDown(object sender, System.Windows.Input.KeyEventArgs e)
    {
        if (sender is not TextBox box) return;
        // Allow editing modifiers / navigation only when capturing a bind.
        if (e.Key is System.Windows.Input.Key.Tab) return;
        e.Handled = true;
        var key = e.Key == System.Windows.Input.Key.System ? e.SystemKey : e.Key;
        if (key is System.Windows.Input.Key.LeftCtrl or System.Windows.Input.Key.RightCtrl
            or System.Windows.Input.Key.LeftAlt or System.Windows.Input.Key.RightAlt
            or System.Windows.Input.Key.LeftShift or System.Windows.Input.Key.RightShift
            or System.Windows.Input.Key.LWin or System.Windows.Input.Key.RWin)
            return;
        box.Text = FormatCaptureKey(key);
    }

    private static string FormatCaptureKey(System.Windows.Input.Key key) => key switch
    {
        System.Windows.Input.Key.Space => "Space",
        System.Windows.Input.Key.Return => "Enter",
        System.Windows.Input.Key.Escape => "Escape",
        System.Windows.Input.Key.Back => "Backspace",
        System.Windows.Input.Key.Left => "Left",
        System.Windows.Input.Key.Right => "Right",
        System.Windows.Input.Key.Up => "Up",
        System.Windows.Input.Key.Down => "Down",
        System.Windows.Input.Key.LeftCtrl => "LeftCtrl",
        System.Windows.Input.Key.RightCtrl => "RightCtrl",
        System.Windows.Input.Key.OemSemicolon => "Semicolon",
        System.Windows.Input.Key.OemQuotes => "Quote",
        System.Windows.Input.Key.OemComma => "Comma",
        System.Windows.Input.Key.OemPeriod => "Period",
        System.Windows.Input.Key.OemMinus => "Minus",
        System.Windows.Input.Key.OemPlus => "Plus",
        _ => key.ToString(),
    };

    private void UpdateKeybindPanelEnabled()
    {
        var on = ChkMnk.IsChecked == true;
        PanelKeybinds.IsEnabled = on;
        BtnResetBinds.IsEnabled = on;
        PanelKeybinds.Opacity = on ? 1.0 : 0.45;
    }

    private void Mnk_Changed(object sender, RoutedEventArgs e)
    {
        if (!_uiReady) return;
        UpdateKeybindPanelEnabled();
    }

    private void ResetBinds_Click(object sender, RoutedEventArgs e)
    {
        foreach (var entry in KeybindCatalog.All)
        {
            if (_keybindBoxes.TryGetValue(entry.Cvar, out var box))
                box.Text = entry.DefaultKeys;
        }
    }

    private void ApplyDefaultsIfEmpty()
    {
        if (string.IsNullOrWhiteSpace(_settings.InstallRoot))
            _settings.InstallRoot = _projectRoot;
        if (string.IsNullOrWhiteSpace(_settings.GameDataRoot))
            _settings.GameDataRoot = Paths.DefaultGameRoot(_projectRoot);
        // Never keep a Debug exe path — xenosd.dll assert-storms in translator.cpp.
        var exe = _settings.GameExePath ?? "";
        var exeIsDebug = exe.Contains("win-amd64-debug", StringComparison.OrdinalIgnoreCase)
                         || exe.Contains($"{Path.DirectorySeparatorChar}debug{Path.DirectorySeparatorChar}",
                             StringComparison.OrdinalIgnoreCase);
        if (string.IsNullOrWhiteSpace(exe) || !File.Exists(exe) || exeIsDebug)
            _settings.GameExePath = Paths.FindGameExe(_projectRoot);
    }

    private void ApplyLocaleFromSettings()
    {
        _l = LauncherLocale.FromGameLanguageId(_settings.UserLanguage);
        _settings.UiLocale = _l.Code;
        ApplyLocale();
    }

    private void ApplyLocale()
    {
        Title = _l["window.title"];
        NavPlay.Content = _l["nav.play"];
        NavSetup.Content = _l["nav.setup"];
        NavVideo.Content = _l["nav.video"];
        NavAudio.Content = _l["nav.audio"];
        NavInput.Content = _l["nav.input"];
        NavLanguage.Content = _l["nav.language"];
        NavAdvanced.Content = _l["nav.advanced"];

        LblPlayTitle.Text = _l["play.title"];
        LblPlaySub.Text = _l["play.sub"];
        LblExe.Text = _l["play.exe"];
        LblGameData.Text = _l["play.data"];
        LblPlayTip.Text = _l["play.tip"];
        BtnPlay.Content = _l["play.btn"];
        BtnOpenFolder.Content = _l["play.open"];
        BtnSave.Content = _l["play.save"];

        LblSetupTitle.Text = _l["setup.title"];
        LblSetupSub.Text = _l["setup.sub"];
        LblIso.Text = _l["setup.iso"];
        BtnBrowseIso.Content = _l["setup.browse"];
        LblInstallTo.Text = _l["setup.install_to"];
        LblXiso.Text = _l["setup.xiso"];
        BtnExtract.Content = _l["setup.extract"];

        LblVideoTitle.Text = _l["video.title"];
        LblVideoSub.Text = _l["video.sub"];
        LblDisplayHead.Text = _l["video.display_head"];
        LblResolution.Text = _l["video.resolution"];
        LblWinW.Text = _l["video.width"];
        LblWinH.Text = _l["video.height"];
        ChkFullscreen.Content = _l["video.fullscreen"];
        ChkLetterbox.Content = _l["video.letterbox"];
        ChkDither.Content = _l["video.dither"];
        LblMonitor.Text = _l["video.monitor"];
        LblVsyncSection.Text = _l["video.vsync_section"];
        ChkVSync.Content = _l["video.vsync"];
        ChkTearing.Content = _l["video.tearing"];
        ChkClockNoScale.Content = _l["video.clock"];
        LblFps.Text = _l["video.fps"];
        LblFpsHint.Text = _l["video.fps_hint"];
        LblUpscale.Text = _l["video.upscale"];
        TxtUpscaleNote.Text = _l["video.upscale_note"];
        LblFsrQuality.Text = _l["video.fsr_quality"];
        LblFsrSharp.Text = _l["video.fsr_sharp"];
        LblFsrPasses.Text = _l["video.fsr_passes"];
        LblCasSharp.Text = _l["video.cas_sharp"];

        LblAudioTitle.Text = _l["audio.title"];
        LblAudioSub.Text = _l["audio.sub"];
        ChkMute.Content = _l["audio.mute"];
        LblAudioQueue.Text = _l["audio.queue"];
        LblAudioHint.Text = _l["audio.hint"];
        RefreshAudioFramesLabel();

        LblInputTitle.Text = _l["input.title"];
        LblInputSub.Text = _l["input.sub"];
        LblInputDeviceHead.Text = _l["input.device_head"];
        LblInputBackend.Text = _l["input.backend"];
        ChkMnk.Content = _l["input.mnk"];
        ChkMnkMouse.Content = _l["input.mnk_mouse"];
        ChkGuide.Content = _l["input.guide"];
        LblSens.Text = _l["input.sens"];
        LblKeybindsHead.Text = _l["input.keybinds_head"];
        LblKeybindsHint.Text = _l["input.keybinds_hint"];
        BtnResetBinds.Content = _l["input.reset_binds"];
        foreach (var entry in KeybindCatalog.All)
        {
            if (_keybindLabels.TryGetValue(entry.Cvar, out var label))
                label.Text = _l[entry.LabelKey];
        }

        LblLangTitle.Text = _l["lang.title"];
        LblLangSub.Text = _l["lang.sub"];
        LblLangField.Text = _l["lang.field"];
        BtnRescanLang.Content = _l["lang.rescan"];

        LblAdvTitle.Text = _l["adv.title"];
        LblAdvSub.Text = _l["adv.sub"];
        LblBoot.Text = _l["adv.boot"];
        ChkSkipBoot.Content = _l["adv.skip_boot"];
        LblSkipHint.Text = _l["adv.skip_hint"];
        LblLogLevel.Text = _l["adv.log_level"];
        ChkVerbose.Content = _l["adv.verbose"];
        ChkLogFile.Content = _l["adv.logfile"];
        LblCountry.Text = _l["adv.country"];
        LblExtra.Text = _l["adv.extra"];
        LblExtraHint.Text = _l["adv.extra_hint"];

        RelabelLanguageCombo();
        RefreshInstallStatus();
        ShowPanel(_activePanel);
    }

    private void RefreshAudioFramesLabel() =>
        TxtAudioQ.Text = _l.Fmt("audio.frames", (int)SldAudioQ.Value);

    private void RelabelLanguageCombo()
    {
        if (CmbLanguage.ItemsSource is not IEnumerable<DetectedLanguage> langs) return;
        var id = (CmbLanguage.SelectedItem as DetectedLanguage)?.Id ?? _settings.UserLanguage;
        var relabeled = langs.Select(l => new DetectedLanguage
        {
            Id = l.Id,
            Code = l.Code,
            StartupToken = l.StartupToken,
            DisplayName = _l.LangDisplayName(l.Id),
        }).ToList();
        CmbLanguage.SelectionChanged -= Language_Changed;
        CmbLanguage.ItemsSource = relabeled;
        CmbLanguage.SelectedItem = relabeled.FirstOrDefault(l => l.Id == id) ?? relabeled.FirstOrDefault();
        CmbLanguage.SelectionChanged += Language_Changed;
        if (CmbLanguage.SelectedItem is DetectedLanguage sel)
        {
            StatusLang.Text = _l.Fmt("lang.status", sel.DisplayName);
            TxtLangHint.Text = relabeled.Count == 1
                ? _l.Fmt("lang.hint_one", sel.DisplayName)
                : _l.Fmt("lang.hint_many", string.Join(", ", relabeled.Select(x => x.DisplayName)));
        }
    }

    private void LoadUiFromSettings()
    {
        TxtIso.Text = "";
        TxtInstallTo.Text = _settings.GameDataRoot;
        TxtXisoTool.Text = Paths.FindExtractXiso(_projectRoot);
        TxtExe.Text = _settings.GameExePath;
        TxtGame.Text = _settings.GameDataRoot;

        SelectCombo(CmbResolution, _settings.Resolution);
        TxtWinW.Text = _settings.WindowWidth.ToString();
        TxtWinH.Text = _settings.WindowHeight.ToString();
        ChkFullscreen.IsChecked = _settings.Fullscreen;
        ChkLetterbox.IsChecked = _settings.PresentLetterbox;
        ChkDither.IsChecked = _settings.PresentDither;
        TxtMonitor.Text = _settings.Monitor.ToString();
        ChkVSync.IsChecked = _settings.VSync;
        ChkTearing.IsChecked = _settings.AllowTearing;
        ChkClockNoScale.IsChecked = _settings.ClockNoScaling;
        // Only 30 / 60 — higher rates make UE3 physics/animation look accelerated.
        var fpsPreset = string.IsNullOrWhiteSpace(_settings.FpsPreset) ? "60" : _settings.FpsPreset;
        if (fpsPreset is not ("30" or "60"))
            fpsPreset = _settings.RefreshRate <= 45 ? "30" : "60";
        SelectCombo(CmbFpsPreset, fpsPreset);
        TxtRefresh.Text = fpsPreset;
        SelectCombo(CmbPresentEffect, _settings.PresentEffect);
        SelectCombo(CmbFsrQuality, _settings.FsrQualityMode);
        SldFsrSharp.Value = Math.Clamp(_settings.FsrSharpnessReduction, 0, 1);
        TxtFsrPasses.Text = _settings.FsrMaxUpsamplingPasses.ToString();
        SldCasSharp.Value = Math.Clamp(_settings.CasAdditionalSharpness, 0, 1);

        ChkMute.IsChecked = _settings.AudioMute;
        SldAudioQ.Value = _settings.AudioMaxQFrames;

        SelectCombo(CmbInputBackend, _settings.InputBackend);
        ChkMnk.IsChecked = _settings.MnkMode;
        ChkMnkMouse.IsChecked = _settings.MnkMouse;
        ChkGuide.IsChecked = _settings.GuideButton;
        SldSens.Value = _settings.MnkSensitivity;
        foreach (var entry in KeybindCatalog.All)
        {
            if (!_keybindBoxes.TryGetValue(entry.Cvar, out var box)) continue;
            box.Text = _settings.Keybinds.TryGetValue(entry.Cvar, out var saved) && !string.IsNullOrWhiteSpace(saved)
                ? saved
                : entry.DefaultKeys;
        }

        ChkSkipBoot.IsChecked = _settings.SkipBootLogos;
        SelectCombo(CmbLogLevel, _settings.LogLevel);
        ChkVerbose.IsChecked = _settings.VerboseLog;
        ChkLogFile.IsChecked = _settings.WriteLogFile;
        TxtCountry.Text = _settings.UserCountry.ToString();
        TxtExtra.Text = _settings.ExtraArgs;
    }

    private void SaveUiToSettings()
    {
        _settings.GameDataRoot = TxtInstallTo.Text.Trim();
        _settings.GameExePath = TxtExe.Text.Trim();
        _settings.Resolution = ComboValue(CmbResolution);
        _ = int.TryParse(TxtWinW.Text, out var ww); _settings.WindowWidth = ww > 0 ? ww : 1920;
        _ = int.TryParse(TxtWinH.Text, out var wh); _settings.WindowHeight = wh > 0 ? wh : 1080;
        _settings.Fullscreen = ChkFullscreen.IsChecked == true;
        _settings.PresentLetterbox = ChkLetterbox.IsChecked == true;
        _settings.PresentDither = ChkDither.IsChecked == true;
        _ = int.TryParse(TxtMonitor.Text, out var mon); _settings.Monitor = mon;
        _settings.VSync = ChkVSync.IsChecked == true;
        _settings.AllowTearing = ChkTearing.IsChecked == true;
        _settings.ClockNoScaling = ChkClockNoScale.IsChecked == true;
        _settings.FpsPreset = ComboValue(CmbFpsPreset);
        ApplyFpsPresetToSettings();
        _settings.PresentEffect = ComboValue(CmbPresentEffect);
        _settings.FsrQualityMode = ComboValue(CmbFsrQuality);
        _settings.FsrSharpnessReduction = SldFsrSharp.Value;
        _ = int.TryParse(TxtFsrPasses.Text, out var passes);
        _settings.FsrMaxUpsamplingPasses = passes > 0 ? passes : 4;
        _settings.CasAdditionalSharpness = SldCasSharp.Value;

        _settings.AudioMute = ChkMute.IsChecked == true;
        _settings.AudioMaxQFrames = (int)SldAudioQ.Value;

        _settings.InputBackend = ComboValue(CmbInputBackend);
        _settings.MnkMode = ChkMnk.IsChecked == true;
        _settings.MnkMouse = ChkMnkMouse.IsChecked == true;
        _settings.GuideButton = ChkGuide.IsChecked == true;
        _settings.MnkSensitivity = SldSens.Value;
        _settings.Keybinds = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var entry in KeybindCatalog.All)
        {
            if (!_keybindBoxes.TryGetValue(entry.Cvar, out var box)) continue;
            var val = box.Text.Trim();
            if (val.Length > 0)
                _settings.Keybinds[entry.Cvar] = val;
        }

        if (CmbLanguage.SelectedItem is DetectedLanguage lang)
            _settings.UserLanguage = lang.Id;

        _settings.SkipBootLogos = ChkSkipBoot.IsChecked == true;
        _settings.LogLevel = ComboValue(CmbLogLevel);
        _settings.VerboseLog = ChkVerbose.IsChecked == true;
        _settings.WriteLogFile = ChkLogFile.IsChecked == true;
        _ = int.TryParse(TxtCountry.Text, out var country); _settings.UserCountry = country;
        _settings.ExtraArgs = TxtExtra.Text.Trim();
        _settings.GpuPlugin = "xenos";
        _settings.UiLocale = _l.Code;
    }

    private void ApplyFpsPresetToSettings()
    {
        var preset = ComboValue(CmbFpsPreset);
        _settings.RefreshRate = preset == "30" ? 30 : 60;
        _settings.FpsPreset = _settings.RefreshRate <= 45 ? "30" : "60";
        // Keep guest clock scaled — uncapped rates make Saw II run fast.
        _settings.ClockNoScaling = false;
        ChkClockNoScale.IsChecked = false;
    }

    private void FpsPreset_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_uiReady) return;
        ApplyFpsPresetToSettings();
    }

    private void UpdateFpsUi()
    {
        PanelCustomFps.Visibility = Visibility.Collapsed;
    }

    private void UpdateUpscaleUi()
    {
        var effect = ComboValue(CmbPresentEffect).ToLowerInvariant();
        var isFsr = effect is "fsr" or "fsr2" or "fsr3";
        var isCas = effect is "cas" || isFsr;
        PanelFsr.Visibility = isFsr ? Visibility.Visible : Visibility.Collapsed;
        PanelCas.Visibility = isCas ? Visibility.Visible : Visibility.Collapsed;
    }

    private void PresentEffect_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_uiReady) return;
        UpdateUpscaleUi();
    }

    private void Language_Changed(object sender, SelectionChangedEventArgs e)
    {
        if (!_uiReady) return;
        if (CmbLanguage.SelectedItem is not DetectedLanguage lang) return;
        _settings.UserLanguage = lang.Id;
        ApplyLocaleFromSettings();
        _settings.Save();
    }

    private void RefreshInstallStatus()
    {
        var ready = File.Exists(Path.Combine(_settings.GameDataRoot, "Default.xex"));
        var exeOk = File.Exists(_settings.GameExePath);
        StatusInstall.Text = ready ? _l["status.ready"] : _l["status.no_dump"];
        StatusInstall.Foreground = ready
            ? (System.Windows.Media.Brush)FindResource("Bone")
            : (System.Windows.Media.Brush)FindResource("Rust");

        TxtExe.Text = _settings.GameExePath + (exeOk ? "" : _l["play.missing_exe"]);
        TxtGame.Text = _settings.GameDataRoot;
        TxtInstallTo.Text = _settings.GameDataRoot;
    }

    private void RescanLanguages(bool preferOs)
    {
        var langs = LanguageCatalog.DetectFromGameRoot(_settings.GameDataRoot);
        uint pick = preferOs ? LanguageCatalog.PreferForOs(langs) : _settings.UserLanguage;
        var localized = langs.Select(l => new DetectedLanguage
        {
            Id = l.Id,
            Code = l.Code,
            StartupToken = l.StartupToken,
            DisplayName = _l.LangDisplayName(l.Id),
        }).ToList();
        var sel = localized.FirstOrDefault(l => l.Id == pick) ?? localized.FirstOrDefault();
        CmbLanguage.SelectionChanged -= Language_Changed;
        CmbLanguage.ItemsSource = localized;
        CmbLanguage.SelectedItem = sel;
        CmbLanguage.SelectionChanged += Language_Changed;
        if (sel != null)
        {
            _settings.UserLanguage = sel.Id;
            StatusLang.Text = _l.Fmt("lang.status", sel.DisplayName);
            TxtLangHint.Text = localized.Count == 1
                ? _l.Fmt("lang.hint_one", sel.DisplayName)
                : _l.Fmt("lang.hint_many", string.Join(", ", localized.Select(l => l.DisplayName)));
        }
    }

    private void ShowPanel(string name)
    {
        _activePanel = name;
        foreach (var kv in _panels)
            kv.Value.Visibility = kv.Key == name ? Visibility.Visible : Visibility.Collapsed;
        foreach (var kv in _nav)
            kv.Value.Tag = kv.Key == name ? "active" : null;
    }

    private void Nav_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not Button b) return;
        foreach (var kv in _nav)
        {
            if (ReferenceEquals(kv.Value, b))
            {
                ShowPanel(kv.Key);
                return;
            }
        }
    }

    private void BrowseIso_Click(object sender, RoutedEventArgs e)
    {
        var dlg = new OpenFileDialog
        {
            Filter = _l["setup.filter"],
            Title = _l["setup.iso_dialog"]
        };
        if (dlg.ShowDialog() == true)
            TxtIso.Text = dlg.FileName;
    }

    private async void Install_Click(object sender, RoutedEventArgs e)
    {
        var iso = TxtIso.Text.Trim();
        var dest = TxtInstallTo.Text.Trim();
        var tool = TxtXisoTool.Text.Trim();
        if (string.IsNullOrWhiteSpace(iso) || !File.Exists(iso))
        {
            TxtInstallStatus.Text = _l["setup.need_iso"];
            return;
        }
        if (string.IsNullOrWhiteSpace(tool) || !File.Exists(tool))
        {
            TxtInstallStatus.Text = _l["setup.need_xiso"];
            return;
        }

        _installCts?.Cancel();
        _installCts = new CancellationTokenSource();
        InstallProgress.Value = 0;
        TxtInstallStatus.Text = _l["setup.starting"];

        var progress = new Progress<IsoInstallProgress>(p =>
        {
            if (p.Fraction >= 0) InstallProgress.Value = p.Fraction;
            var phaseKey = "setup.phase." + p.Phase;
            var phaseText = _l[phaseKey];
            TxtInstallStatus.Text = string.IsNullOrWhiteSpace(p.Detail)
                ? phaseText
                : $"{phaseText} {p.Detail}";
        });

        try
        {
            var installer = new IsoInstaller();
            await installer.InstallAsync(iso, dest, tool, progress, _installCts.Token);
            _settings.GameDataRoot = dest;
            RefreshInstallStatus();
            RescanLanguages(preferOs: true);
            ApplyLocaleFromSettings();
            SaveUiToSettings();
            _settings.Save();
            TxtInstallStatus.Text = _l["setup.done"];
            ShowPanel("LANGUAGE");
        }
        catch (OperationCanceledException)
        {
            TxtInstallStatus.Text = _l["setup.cancelled"];
        }
        catch (Exception ex)
        {
            TxtInstallStatus.Text = _l.Fmt("setup.failed", ex.Message);
            MessageBox.Show(ex.Message, _l["setup.failed_title"], MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void RescanLang_Click(object sender, RoutedEventArgs e)
    {
        SaveUiToSettings();
        RescanLanguages(preferOs: false);
        ApplyLocaleFromSettings();
    }

    private void Play_Click(object sender, RoutedEventArgs e)
    {
        try
        {
            SaveUiToSettings();
            _settings.Save();
            RefreshInstallStatus();
            GameProcess.Start(_settings);
            TxtPlayMsg.Text = _l["play.launched"];
        }
        catch (Exception ex)
        {
            TxtPlayMsg.Text = ex.Message;
            MessageBox.Show(ex.Message, _l["play.cannot"], MessageBoxButton.OK, MessageBoxImage.Warning);
        }
    }

    private void Save_Click(object sender, RoutedEventArgs e)
    {
        SaveUiToSettings();
        _settings.Save();
        TxtPlayMsg.Text = _l.Fmt("play.saved", LauncherSettings.DefaultPath());
    }

    private void OpenGameFolder_Click(object sender, RoutedEventArgs e)
    {
        var path = _settings.GameDataRoot;
        if (!Directory.Exists(path))
        {
            MessageBox.Show(_l["play.missing_folder"], _l["play.missing_folder_title"]);
            return;
        }
        Process.Start(new ProcessStartInfo { FileName = path, UseShellExecute = true });
    }

    private static void SelectCombo(ComboBox box, string value)
    {
        foreach (var item in box.Items)
        {
            if (item is ComboBoxItem cbi)
            {
                var tag = cbi.Tag?.ToString() ?? "";
                var text = cbi.Content?.ToString() ?? "";
                if (string.Equals(tag, value, StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(text, value, StringComparison.OrdinalIgnoreCase))
                {
                    box.SelectedItem = item;
                    return;
                }
            }
            else if (string.Equals(item?.ToString(), value, StringComparison.OrdinalIgnoreCase))
            {
                box.SelectedItem = item;
                return;
            }
        }
        if (box.Items.Count > 0) box.SelectedIndex = 0;
    }

    private static string ComboValue(ComboBox box) =>
        box.SelectedItem switch
        {
            ComboBoxItem cbi when cbi.Tag is string tag && tag.Length > 0 => tag,
            ComboBoxItem cbi => cbi.Content?.ToString() ?? "",
            string s => s,
            DetectedLanguage l => l.DisplayName,
            _ => box.Text ?? ""
        };
}