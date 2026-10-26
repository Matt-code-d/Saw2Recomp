using System.Buffers.Binary;

namespace Saw2Launcher.Services;

/// <summary>
/// Unskippable company logos only. Truncates each original .bik to 2 frames
/// (brief flash of the REAL logo) instead of swapping in unrelated movies.
/// </summary>
public static class BootLogoSkip
{
    public const string Marker = ".saw2bootskip";
    private const int KeepFrames = 2;

    private static readonly string[] BootStems =
    [
        "startup_konami",
        "Zombie-720p",
        "Lionsgate",
        "Twisted",
    ];

    public static string MoviesDir(string gameDataRoot)
    {
        var a = Path.Combine(gameDataRoot, "SAW2GAME", "Movies");
        if (Directory.Exists(a)) return a;
        return Path.Combine(gameDataRoot, "Saw2Game", "Movies");
    }

    public static void Apply(string gameDataRoot)
    {
        var dir = MoviesDir(gameDataRoot);
        if (!Directory.Exists(dir)) return;

        RestoreLegacyExtras(dir);

        foreach (var stem in BootStems)
        {
            var path = Path.Combine(dir, stem + ".bik");
            var bak = path + Marker;

            if (File.Exists(path) && !File.Exists(bak))
                File.Move(path, bak);

            if (!File.Exists(bak))
                continue;

            TruncateBik(bak, path, KeepFrames);
        }
    }

    public static void Restore(string gameDataRoot)
    {
        foreach (var dir in CandidateMovieDirs(gameDataRoot))
        {
            RestoreLegacyExtras(dir);

            foreach (var bak in Directory.EnumerateFiles(dir, "*.bik" + Marker))
            {
                var original = bak[..^Marker.Length];
                try
                {
                    if (File.Exists(original))
                        File.Delete(original);
                    File.Move(bak, original);
                }
                catch { /* next launch */ }
            }

            foreach (var stem in BootStems)
            {
                foreach (var ext in new[] { ".xxx", ".txt" })
                {
                    var bak = Path.Combine(dir, stem + ext + Marker);
                    if (!File.Exists(bak)) continue;
                    var original = Path.Combine(dir, stem + ext);
                    if (!File.Exists(original))
                        File.Move(bak, original);
                    else
                        File.Delete(bak);
                }
            }
        }
    }

    public static void TruncateBik(string srcPath, string dstPath, int keepFrames)
    {
        var bytes = File.ReadAllBytes(srcPath);
        if (bytes.Length < 0x30)
            throw new InvalidDataException("BIK too small: " + srcPath);

        var sig = System.Text.Encoding.ASCII.GetString(bytes, 0, 3);
        if (sig != "BIK")
            throw new InvalidDataException("Not a BIK: " + srcPath);

        var frames = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(8));
        var tracks = BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0x28));
        if (keepFrames < 1) keepFrames = 1;
        if (keepFrames > (int)frames) keepFrames = (int)frames;

        var headerBase = 0x2C + (int)tracks * 12;
        var indexOff = headerBase;

        uint Off(int i) =>
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(indexOff + i * 4)) & 0xFFFF_FFFEu;
        bool Key(int i) =>
            (BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(indexOff + i * 4)) & 1u) != 0;

        var startData = (int)Off(0);
        var endData = (int)Off(keepFrames);
        if (endData <= startData || endData > bytes.Length)
            throw new InvalidDataException("Bad BIK frame index: " + srcPath);

        var payloadLen = endData - startData;
        uint maxFrame = 0;
        for (var i = 0; i < keepFrames; i++)
        {
            var sz = Off(i + 1) - Off(i);
            if (sz > maxFrame) maxFrame = sz;
        }

        var newHeaderSize = headerBase + (keepFrames + 1) * 4;
        var newFileSize = newHeaderSize + payloadLen;
        var output = new byte[newFileSize];
        Buffer.BlockCopy(bytes, 0, output, 0, headerBase);

        BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(4), (uint)(newFileSize - 8));
        BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(8), (uint)keepFrames);
        BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(0x0C), maxFrame);
        BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(0x10), (uint)keepFrames);

        for (var i = 0; i <= keepFrames; i++)
        {
            var rel = Off(i) - (uint)startData;
            var abs = (uint)(newHeaderSize + rel);
            if (i < keepFrames && (Key(i) || i == 0))
                abs |= 1u;
            BinaryPrimitives.WriteUInt32LittleEndian(output.AsSpan(headerBase + i * 4), abs);
        }

        Buffer.BlockCopy(bytes, startData, output, newHeaderSize, payloadLen);
        File.WriteAllBytes(dstPath, output);
    }

    private static void RestoreLegacyExtras(string dir)
    {
        foreach (var stem in new[] { "Saw2Splash", "Saw2Mid" })
        {
            var bak = Path.Combine(dir, stem + ".bik" + Marker);
            if (!File.Exists(bak)) continue;
            var original = Path.Combine(dir, stem + ".bik");
            try
            {
                if (File.Exists(original)) File.Delete(original);
                File.Move(bak, original);
            }
            catch { /* ignore */ }
        }
    }

    private static IEnumerable<string> CandidateMovieDirs(string gameDataRoot)
    {
        var a = Path.Combine(gameDataRoot, "SAW2GAME", "Movies");
        var b = Path.Combine(gameDataRoot, "Saw2Game", "Movies");
        if (Directory.Exists(a)) yield return a;
        if (Directory.Exists(b) && !string.Equals(a, b, StringComparison.OrdinalIgnoreCase))
            yield return b;
    }
}