using System.Text;

namespace NEMEngine.Distribution;

// 共通除外とLFS規則を既存設定へ追加する
internal static class GitProjectAttributes {
    private const string Begin = "# NEMLauncher LFS begin";
    private const string End = "# NEMLauncher LFS end";
    private static readonly HashSet<string> BinaryExtensions = new(StringComparer.OrdinalIgnoreCase) {
        ".fbx", ".obj", ".glb", ".bin", ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".hdr", ".exr", ".ktx", ".ktx2",
        ".tif", ".tiff", ".webp", ".wav", ".ogg", ".mp3", ".flac", ".mp4", ".webm", ".ttf", ".otf"
    };

    public static bool UsesLFS(string path, long bytes) => BinaryExtensions.Contains(System.IO.Path.GetExtension(path)) || bytes >= 50L * 1048576;

    public static void Reset(string root) {
        foreach (string name in new[] { ".gitignore", ".gitattributes" })
            DistributionStorage.WriteText(Path.Combine(root, name), ReadTemplate(name));
    }

    public static void Update(string root, IEnumerable<GitUploadFile> files) {
        MergeTemplate(root, ".gitignore");
        MergeTemplate(root, ".gitattributes");
        string path = Path.Combine(root, ".gitattributes");
        string original = File.ReadAllText(path);
        var retained = new HashSet<string>(StringComparer.Ordinal);
        int begin = original.IndexOf(Begin, StringComparison.Ordinal);
        if (begin >= 0) {
            int end = original.IndexOf(End, begin, StringComparison.Ordinal);
            if (end < 0) throw new InvalidDataException("LFS設定の終端がありません .gitattributesを確認してください");
            foreach (string line in original[(begin + Begin.Length)..end].Split('\n'))
                if (line.Trim().EndsWith(" filter=lfs diff=lfs merge=lfs -text", StringComparison.Ordinal)) retained.Add(line.Trim());
            original = original.Remove(begin, end + End.Length - begin);
        }
        foreach (string extension in BinaryExtensions.Order()) {
            string pattern = "*." + string.Concat(extension[1..].Select(character => char.IsLetter(character)
                ? "[" + char.ToLowerInvariant(character) + char.ToUpperInvariant(character) + "]" : character.ToString()));
            retained.Add(pattern + " filter=lfs diff=lfs merge=lfs -text");
        }
        foreach (var file in files.Where(file => file.Bytes >= 50L * 1048576 && !BinaryExtensions.Contains(Path.GetExtension(file.Path)))) {
            string literal = "/" + file.Path.Replace('\\', '/').Replace("[", "\\[").Replace("]", "\\]");
            retained.Add("\"" + literal.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\" filter=lfs diff=lfs merge=lfs -text");
        }
        // 一度LFSへ渡したファイルは縮小後も同じ保存方式を使う
        var rules = new StringBuilder(original.TrimEnd()).Append('\n').AppendLine(Begin);
        foreach (string rule in retained.Order(StringComparer.Ordinal)) rules.AppendLine(rule);
        rules.AppendLine(End);
        DistributionStorage.WriteText(path, rules.ToString());
    }

    public static void MergeTemplate(string root, string name) {
        string path = Path.Combine(root, name);
        string original = File.Exists(path) ? File.ReadAllText(path) : "";
        var existing = original.Split('\n').Select(line => line.Trim()).ToHashSet(StringComparer.Ordinal);
        var missing = ReadTemplate(name).Split('\n').Select(line => line.Trim()).Where(line => line.Length > 0 && !existing.Contains(line)).ToArray();
        if (missing.Length == 0) return;
        DistributionStorage.WriteText(path, original.TrimEnd() + "\n" + string.Join('\n', missing) + "\n");
    }

    private static string ReadTemplate(string name) {
        using var stream = typeof(GitProjectAttributes).Assembly.GetManifestResourceStream("NEMEngine.Project" + name)!;
        using var reader = new StreamReader(stream);
        return reader.ReadToEnd();
    }
}
