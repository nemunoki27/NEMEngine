using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace NEMEngine.Distribution;

public static partial class DistributionStorage {
    public static JsonSerializerOptions JsonOptions { get; } = new() {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        WriteIndented = true
    };

    public static T Read<T>(string path) where T : class {
        return JsonSerializer.Deserialize<T>(File.ReadAllText(path), JsonOptions)
            ?? throw new InvalidDataException("設定を読み込めません: " + path);
    }

    public static void Write<T>(string path, T value) {
        WriteText(path, JsonSerializer.Serialize(value, JsonOptions));
    }

    public static string Child(string root, string relative) {
        if (string.IsNullOrWhiteSpace(relative) || Path.IsPathRooted(relative) || relative.Contains(':'))
            throw new InvalidDataException("相対パスが不正です: " + relative);
        string normalized = relative.Replace('\\', '/');
        if (normalized.Split('/').Any(part => part == ".." || part.EndsWith(' ') || part.EndsWith('.')))
            throw new InvalidDataException("相対パスが不正です: " + relative);
        string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
        string result = Path.GetFullPath(Path.Combine(fullRoot, normalized));
        if (!result.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException("保存先がフォルダーの外にあります");
        return result;
    }

    public static void RequireVersion(string version) {
        if (!Version.TryParse(version, out var parsed) || parsed.Revision != -1 || parsed.Build < 0 || parsed.ToString(3) != version)
            throw new InvalidDataException("バージョンは0.1.0の形式で指定してください");
    }

    public static void RequireID(string id) {
        if (!IDPattern().IsMatch(id)) throw new InvalidDataException("配布物の識別子が不正です");
    }

    public static async Task<string> HashAsync(string path, CancellationToken token = default) {
        await using var stream = File.OpenRead(path);
        return Convert.ToHexString(await SHA256.HashDataAsync(stream, token)).ToLowerInvariant();
    }

    public static bool IsInside(string path, string root) {
        string full = Path.GetFullPath(path);
        string parent = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar);
        return full.Equals(parent, StringComparison.OrdinalIgnoreCase) ||
            full.StartsWith(parent + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    public static void CopyTree(string source, string destination) {
        if ((File.GetAttributes(source) & FileAttributes.ReparsePoint) != 0)
            throw new IOException("リンクを含むフォルダーはコピーできません: " + source);
        Directory.CreateDirectory(destination);
        foreach (string entry in Directory.EnumerateFileSystemEntries(source)) {
            if ((File.GetAttributes(entry) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("リンクを含むフォルダーはコピーできません: " + entry);
            string target = Child(destination, Path.GetFileName(entry));
            if (Directory.Exists(entry)) CopyTree(entry, target);
            else {
                File.Copy(entry, target, false);
                // 共通テンプレートの読み取り専用属性を持ち込まない
                File.SetAttributes(target, File.GetAttributes(target) & ~FileAttributes.ReadOnly);
            }
        }
    }

    public static void WriteText(string path, string content) {
        // IDEが監視する参照設定を同じ内容で再生成しない
        if (File.Exists(path) && File.ReadAllText(path) == content) return;
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        string stage = path + ".writing-" + Guid.NewGuid().ToString("N");
        try {
            File.WriteAllText(stage, content);
            File.Move(stage, path, true);
        } finally { if (File.Exists(stage)) File.Delete(stage); }
    }

    // 所有する作業フォルダーだけを削除する
    public static void RemoveTree(string directory, string ownerRoot) {
        if (!IsInside(directory, ownerRoot) || Path.GetFullPath(directory).Equals(Path.GetFullPath(ownerRoot), StringComparison.OrdinalIgnoreCase))
            throw new IOException("作業フォルダーの削除先が不正です");
        if (!Directory.Exists(directory)) return;
        if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) throw new IOException("リンク先は削除できません");
        foreach (string entry in Directory.EnumerateFileSystemEntries(directory)) {
            if ((File.GetAttributes(entry) & FileAttributes.ReparsePoint) != 0) throw new IOException("リンクを含むため削除できません");
            if (Directory.Exists(entry)) RemoveTree(entry, ownerRoot);
            else { File.SetAttributes(entry, FileAttributes.Normal); File.Delete(entry); }
        }
        Directory.Delete(directory);
    }

    [GeneratedRegex(@"^[a-zA-Z0-9][a-zA-Z0-9.-]{0,127}$")]
    private static partial Regex IDPattern();
}
