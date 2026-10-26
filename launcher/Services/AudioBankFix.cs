namespace Saw2Launcher.Services;

/// <summary>
/// UE3 probes CookedXenon/&lt;Language&gt;/*.bnk first; Saw II ships many banks only in CookedXenon root.
/// Hardlink missing language-folder banks to the root copies (no disk duplication).
/// </summary>
public static class AudioBankFix
{
    private static readonly string[] LangFolders =
    [
        "English(US)",
        "Italian",
        "German",
        "French(France)",
        "Spanish(Spain)",
    ];

    public static void EnsureLanguageBankLinks(string gameDataRoot)
    {
        var cooked = Path.Combine(gameDataRoot, "SAW2GAME", "CookedXenon");
        if (!Directory.Exists(cooked))
            cooked = Path.Combine(gameDataRoot, "Saw2Game", "CookedXenon");
        if (!Directory.Exists(cooked)) return;

        string[] rootBnks;
        try { rootBnks = Directory.GetFiles(cooked, "*.bnk"); }
        catch { return; }
        if (rootBnks.Length == 0) return;

        foreach (var lang in LangFolders)
        {
            var dir = Path.Combine(cooked, lang);
            if (!Directory.Exists(dir)) continue;
            foreach (var src in rootBnks)
            {
                var dest = Path.Combine(dir, Path.GetFileName(src));
                if (File.Exists(dest)) continue;
                try
                {
                    CreateHardLink(dest, src);
                }
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