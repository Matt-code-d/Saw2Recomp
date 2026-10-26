namespace Saw2Launcher.Services;

/// <summary>
/// UE3 probes Movies/&lt;stem&gt;_&lt;LANG&gt;.txt for Bink subtitle tracks (e.g. lvla_p_ITA.txt).
/// Retail disc usually ships only the unsuffixed .txt. Hardlink aliases so subtitles resolve.
/// Does not touch .bik files.
/// </summary>
public static class MovieSubtitleFix
{
    private static readonly string[] LangTokens = ["ITA", "DEU", "FRA", "ESN", "INT", "JPN"];

    public static void EnsureLanguageSubtitleLinks(string gameDataRoot)
    {
        var movies = Path.Combine(gameDataRoot, "SAW2GAME", "Movies");
        if (!Directory.Exists(movies))
            movies = Path.Combine(gameDataRoot, "Saw2Game", "Movies");
        if (!Directory.Exists(movies)) return;

        string[] txts;
        try { txts = Directory.GetFiles(movies, "*.txt"); }
        catch { return; }

        foreach (var src in txts)
        {
            var name = Path.GetFileNameWithoutExtension(src);
            // Skip files that are already language-suffixed.
            if (LangTokens.Any(t => name.EndsWith("_" + t, StringComparison.OrdinalIgnoreCase)))
                continue;

            foreach (var tok in LangTokens)
            {
                var dest = Path.Combine(movies, $"{name}_{tok}.txt");
                if (File.Exists(dest)) continue;
                try { CreateHardLink(dest, src); }
                catch
                {
                    try { File.Copy(src, dest); } catch { /* ignore */ }
                }
            }
        }
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    private static extern bool CreateHardLink(string lpFileName, string lpExistingFileName, IntPtr lpSecurityAttributes);

    private static void CreateHardLink(string dest, string src)
    {
        if (!CreateHardLink(dest, src, IntPtr.Zero))
            throw new IOException("CreateHardLink failed for " + dest);
    }
}