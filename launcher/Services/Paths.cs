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

        // Fallback: assume packaged next to game/
        return Directory.GetParent(AppContext.BaseDirectory)?.FullName
               ?? AppContext.BaseDirectory;
    }

    public static string DefaultGameRoot(string projectRoot) =>
        Path.Combine(projectRoot, "game");

    public static string FindGameExe(string projectRoot)
    {
        string[] candidates =
        [
            Path.Combine(projectRoot, "out", "build", "win-amd64-relwithdebinfo", "saw2.exe"),
            Path.Combine(projectRoot, "out", "build", "win-amd64-release", "saw2.exe"),
            Path.Combine(projectRoot, "out", "build", "win-amd64-debug", "saw2.exe"),
            Path.Combine(projectRoot, "bin", "saw2.exe"),
            Path.Combine(AppContext.BaseDirectory, "saw2.exe"),
        ];
        return candidates.FirstOrDefault(File.Exists) ?? candidates[0];
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
