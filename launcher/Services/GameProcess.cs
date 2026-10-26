using System.Diagnostics;
using Saw2Launcher.Models;

namespace Saw2Launcher.Services;

public static class GameProcess
{
    public static Process Start(LauncherSettings settings)
    {
        if (!File.Exists(settings.GameExePath))
            throw new FileNotFoundException("saw2.exe not found. Build RelWithDebInfo first.", settings.GameExePath);
        if (!Directory.Exists(settings.GameDataRoot) ||
            !File.Exists(Path.Combine(settings.GameDataRoot, "Default.xex")))
            throw new InvalidOperationException("Game data missing. Run Setup from your Xbox 360 ISO first.");

        AudioBankFix.EnsureLanguageBankLinks(settings.GameDataRoot);
        MovieSubtitleFix.EnsureLanguageSubtitleLinks(settings.GameDataRoot);
        BootLogoSkip.Restore(settings.GameDataRoot);
        if (settings.SkipBootLogos)
            BootLogoSkip.Apply(settings.GameDataRoot);

        var psi = new ProcessStartInfo
        {
            FileName = settings.GameExePath,
            WorkingDirectory = Path.GetDirectoryName(settings.GameExePath)!,
            UseShellExecute = false,
        };
        foreach (var a in settings.ToProcessArguments())
            psi.ArgumentList.Add(a);

        // Flush logs often so FATAL addresses survive hard crashes.
        if (settings.WriteLogFile)
            psi.ArgumentList.Add("--log_flush_interval=1");

        EnsureRuntimeDlls(Path.GetDirectoryName(settings.GameExePath)!);

        var p = Process.Start(psi);
        if (p == null) throw new InvalidOperationException("Failed to start saw2.exe");

        if (settings.SkipBootLogos)
        {
            var root = settings.GameDataRoot;
            _ = Task.Run(() =>
            {
                try { p.WaitForExit(); }
                catch { /* ignore */ }
                try { BootLogoSkip.Restore(root); }
                catch { /* ignore */ }
            });
        }

        return p;
    }

    private static void EnsureRuntimeDlls(string exeDir)
    {
        var projectRoot = Paths.FindProjectRoot();
        var sdkBin = Path.GetFullPath(Path.Combine(projectRoot, "..", "tools", "rexglue-sdk-bin", "win-amd64", "bin"));
        if (!Directory.Exists(sdkBin)) return;

        string[] pairs =
        [
            "rexruntimerd.dll",
            "rexgpu-xenosrd.dll",
            "TracyClientrd.dll",
            "rexruntimed.dll",
            "rexgpu-xenosd.dll",
            "TracyClientd.dll",
            "rexruntime.dll",
            "rexgpu-xenos.dll",
            "TracyClient.dll",
        ];
        foreach (var name in pairs)
        {
            var src = Path.Combine(sdkBin, name);
            var dst = Path.Combine(exeDir, name);
            if (File.Exists(src) && !File.Exists(dst))
                File.Copy(src, dst, true);
        }
    }
}