using Saw2Launcher.Models;

namespace Saw2Launcher.Services;

public static class LanguageCatalog
{
    // Xbox 360 XLanguage + UE3 Startup_*.xxx tokens present on Saw2 discs.
    public static readonly DetectedLanguage[] All =
    [
        new() { Id = 1, Code = "en", DisplayName = "English", StartupToken = "INT" },
        new() { Id = 3, Code = "de", DisplayName = "Deutsch", StartupToken = "DEU" },
        new() { Id = 4, Code = "fr", DisplayName = "Français", StartupToken = "FRA" },
        new() { Id = 5, Code = "es", DisplayName = "Español", StartupToken = "ESN" },
        new() { Id = 6, Code = "it", DisplayName = "Italiano", StartupToken = "ITA" },
    ];

    public static IReadOnlyList<DetectedLanguage> DetectFromGameRoot(string gameRoot)
    {
        var cooked = Path.Combine(gameRoot, "SAW2GAME", "CookedXenon");
        if (!Directory.Exists(cooked))
            cooked = Path.Combine(gameRoot, "Saw2Game", "CookedXenon");

        var found = new List<DetectedLanguage>();
        foreach (var lang in All)
        {
            var startup = Path.Combine(cooked, $"Startup_{lang.StartupToken}.xxx");
            var folderHints = lang.StartupToken switch
            {
                "INT" => new[] { "English(US)", "English" },
                "ITA" => new[] { "Italian" },
                "DEU" => new[] { "German" },
                "FRA" => new[] { "French(France)", "French" },
                "ESN" => new[] { "Spanish(Spain)", "Spanish" },
                _ => Array.Empty<string>()
            };

            var ok = File.Exists(startup) ||
                     folderHints.Any(h => Directory.Exists(Path.Combine(cooked, h)));
            if (ok) found.Add(lang);
        }

        return found.Count > 0 ? found : All.Take(1).ToList();
    }

    public static uint PreferForOs(IReadOnlyList<DetectedLanguage> available)
    {
        var culture = System.Globalization.CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
        var match = available.FirstOrDefault(l =>
            string.Equals(l.Code, culture, StringComparison.OrdinalIgnoreCase));
        if (match != null) return match.Id;
        var en = available.FirstOrDefault(l => l.Id == 1);
        return en?.Id ?? available[0].Id;
    }
}
