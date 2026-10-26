using System.Diagnostics;
using System.Text;

namespace Saw2Launcher.Services;

public sealed class IsoInstallProgress
{
    public string Phase { get; init; } = "";
    public string Detail { get; init; } = "";
    public double Fraction { get; init; } // 0..1 or -1 indeterminate
}

public sealed class IsoInstaller
{
    public async Task InstallAsync(
        string isoPath,
        string gameRoot,
        string extractXisoPath,
        IProgress<IsoInstallProgress>? progress,
        CancellationToken ct)
    {
        if (!File.Exists(isoPath))
            throw new FileNotFoundException("ISO not found", isoPath);
        if (!File.Exists(extractXisoPath))
            throw new FileNotFoundException(
                "extract-xiso.exe not found. Place it under tools/extract-xiso/artifacts/",
                extractXisoPath);

        progress?.Report(new IsoInstallProgress
        {
            Phase = "prepare",
            Detail = "",
            Fraction = 0.05
        });

        if (Directory.Exists(gameRoot))
        {
            // Keep a clean install target
            var bak = gameRoot + "_old_" + DateTime.Now.ToString("yyyyMMdd_HHmmss");
            if (Directory.EnumerateFileSystemEntries(gameRoot).Any())
            {
                progress?.Report(new IsoInstallProgress
                {
                    Phase = "backup",
                    Detail = "",
                    Fraction = 0.1
                });
                Directory.Move(gameRoot, bak);
            }
        }
        Directory.CreateDirectory(gameRoot);

        var staging = Path.Combine(Path.GetTempPath(), "saw2_iso_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(staging);

        try
        {
            progress?.Report(new IsoInstallProgress
            {
                Phase = "extract",
                Detail = "",
                Fraction = 0.2
            });

            // extract-xiso extracts into CWD as "<iso name>\" when -d is unreliable on older builds.
            var psi = new ProcessStartInfo
            {
                FileName = extractXisoPath,
                Arguments = $"-x \"{isoPath}\"",
                WorkingDirectory = staging,
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
            };

            using var proc = new Process { StartInfo = psi, EnableRaisingEvents = true };
            var sb = new StringBuilder();
            proc.OutputDataReceived += (_, e) =>
            {
                if (e.Data == null) return;
                sb.AppendLine(e.Data);
                progress?.Report(new IsoInstallProgress
                {
                    Phase = "extract",
                    Detail = TrimProgressLine(e.Data),
                    Fraction = 0.2
                });
            };
            proc.ErrorDataReceived += (_, e) =>
            {
                if (e.Data == null) return;
                sb.AppendLine(e.Data);
            };

            if (!proc.Start())
                throw new InvalidOperationException("Failed to start extract-xiso");

            proc.BeginOutputReadLine();
            proc.BeginErrorReadLine();
            await proc.WaitForExitAsync(ct);

            // Locate Default.xex under staging
            progress?.Report(new IsoInstallProgress
            {
                Phase = "locate",
                Detail = "",
                Fraction = 0.75
            });

            var xex = Directory.EnumerateFiles(staging, "Default.xex", SearchOption.AllDirectories)
                .FirstOrDefault();
            if (xex == null)
                throw new InvalidOperationException(
                    "Extraction finished but Default.xex was not found. Check that the ISO is a valid Xbox 360 dump.\n\n" +
                    sb.ToString());

            var discRoot = Path.GetDirectoryName(xex)!;

            progress?.Report(new IsoInstallProgress
            {
                Phase = "copy",
                Detail = "",
                Fraction = 0.85
            });

            CopyDirectory(discRoot, gameRoot, ct);

            if (!File.Exists(Path.Combine(gameRoot, "Default.xex")))
                throw new InvalidOperationException("Copy incomplete: Default.xex missing in game root.");

            progress?.Report(new IsoInstallProgress
            {
                Phase = "done",
                Detail = "",
                Fraction = 1.0
            });
        }
        finally
        {
            try { if (Directory.Exists(staging)) Directory.Delete(staging, true); } catch { /* ignore */ }
        }
    }

    private static string TrimProgressLine(string line)
    {
        line = line.Trim();
        if (line.Length > 120) line = line[^120..];
        return line;
    }

    private static void CopyDirectory(string source, string dest, CancellationToken ct)
    {
        Directory.CreateDirectory(dest);
        foreach (var file in Directory.EnumerateFiles(source))
        {
            ct.ThrowIfCancellationRequested();
            File.Copy(file, Path.Combine(dest, Path.GetFileName(file)), true);
        }
        foreach (var dir in Directory.EnumerateDirectories(source))
        {
            ct.ThrowIfCancellationRequested();
            CopyDirectory(dir, Path.Combine(dest, Path.GetFileName(dir)), ct);
        }
    }
}
