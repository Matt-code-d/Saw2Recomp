namespace Saw2Launcher.Services;

public static class Paths
{
    /// <summary>saw2-recomp root (parent of launcher/ when running from source; or next to packaged layout).</summary>
    public static string FindProjectRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        for (var i = 0; i < 8 && dir != null; i++, dir = dir.Parent)
        {
            var candidate = dir.FullName;
            if (File.Exists(Path.Combine(candidate, "saw2_manifest.toml")))
                return candidate;
            if (Directory.Exists(Path.Combine(candidate, "game")) &&
                File.Exists(Path.Combine(candidate, "game", "Default.xex")))
                return candidate;
        }

        // Packaged release: launcher + saw2.exe in the same folder (game/ created by Setup).
        var baseDir = Path.GetFullPath(AppContext.BaseDirectory);
        if (File.Exists(Path.Combine(baseDir, "saw2.exe")))
            return baseDir;

        return Directory.GetParent(baseDir)?.FullName ?? baseDir;
    }

    public static string DefaultGameRoot(string projectRoot) =>
        Path.Combine(projectRoot, "game");

    public static string FindGameExe(string projectRoot)
    {
        // Never pick Debug for play — rexgpu-xenosd.dll asserts in the shader translator.
        string[] candidates =
        [
            Path.Combine(AppContext.BaseDirectory, "saw2.exe"),
            Path.Combine(projectRoot, "out", "build", "win-amd64-relwithdebinfo", "saw2.exe"),
            Path.Combine(projectRoot, "out", "build", "win-amd64-release", "saw2.exe"),
            Path.Combine(projectRoot, "bin", "saw2.exe"),
        ];
        var hit = candidates.FirstOrDefault(File.Exists);
        if (hit != null) return hit;

        // Last resort: Debug exists but we refuse to auto-select it.
        return candidates[0];
    }

    public static string FindExtractXiso(string projectRoot)
    {
        string[] candidates =
        [
            Path.Combine(projectRoot, "..", "tools", "extract-xiso", "artifacts", "extract-xiso.exe"),
            Path.Combine(projectRoot, "tools", "extract-xiso", "extract-xiso.exe"),
            Path.Combine(AppContext.BaseDirectory, "tools", "extract-xiso.exe"),
            Path.Combine(AppContext.BaseDirectory, "extract-xiso.exe"),
        ];
        foreach (var c in candidates)
        {
            var full = Path.GetFullPath(c);
            if (File.Exists(full)) return full;
        }
        return "";
    }
}
