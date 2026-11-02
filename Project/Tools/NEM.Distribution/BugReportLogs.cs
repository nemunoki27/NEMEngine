using System.Text;
using System.Text.RegularExpressions;

namespace NEMEngine.Distribution;

public sealed record BugReportLogFile(string Kind, string Name, long Bytes, string Status);

// 添付が有効な場合だけ指定ログを読み取る
public static class BugReportLogs {
    public const int MaxLogBytes = 1048576;
    private static IEnumerable<(string Kind, string Root, string Relative)> Targets(string launcherRoot, ProjectEntry? project) {
        yield return ("Launcher", launcherRoot, "Logs/launcher.log");
        if (project != null) {
            string root = Path.GetDirectoryName(Path.GetFullPath(project.Descriptor))!;
            yield return ("Engine", root, "Saved/Logs/engine.log");
            yield return ("Game", root, "Saved/Logs/gameLogic.log");
        }
    }

    private static string SafePath(string root, string relative) {
        string path = DistributionStorage.Child(root, relative);
        for (string? current = path; current != null && DistributionStorage.IsInside(current, root); current = Path.GetDirectoryName(current))
            if ((File.Exists(current) || Directory.Exists(current)) && (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                throw new IOException("リンク先のログは添付できません");
        return path;
    }

    public static IReadOnlyList<BugReportLogFile> Describe(string root, ProjectEntry? project) => Targets(root, project).Select(target => {
        string name = Path.GetFileName(target.Relative);
        try {
            string path = SafePath(target.Root, target.Relative);
            return File.Exists(path) ? new BugReportLogFile(target.Kind, name, new FileInfo(path).Length, "") : new(target.Kind, name, 0, "見つかりません");
        } catch (Exception error) when (error is IOException or UnauthorizedAccessException) { return new BugReportLogFile(target.Kind, name, 0, "読み取れません"); }
    }).ToArray();

    public static IReadOnlyList<BugReportLog> Capture(string root, ProjectEntry? project, IEnumerable<string> privatePaths, CancellationToken token) {
        var logs = new List<BugReportLog>();
        foreach (var target in Targets(root, project)) {
            token.ThrowIfCancellationRequested();
            try {
                using var file = new FileStream(SafePath(target.Root, target.Relative), FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                bool truncated = file.Length > MaxLogBytes;
                byte[] bytes = new byte[(int)Math.Min(file.Length, MaxLogBytes)];
                if (truncated) {
                    file.ReadExactly(bytes.AsSpan(0, 65536));
                    file.Position = file.Length - (MaxLogBytes - 65536);
                    file.ReadExactly(bytes.AsSpan(65536));
                } else file.ReadExactly(bytes);
                string text = truncated ? Encoding.UTF8.GetString(bytes, 0, 65536) + "\n[ログの一部を省略]\n" + Encoding.UTF8.GetString(bytes, 65536, bytes.Length - 65536) : Encoding.UTF8.GetString(bytes);
                text = Redact(text, privatePaths);
                text = Limit(text, ref truncated);
                logs.Add(new(target.Kind, text, truncated));
            } catch (Exception error) when (error is IOException or UnauthorizedAccessException) { }
        }
        return logs;
    }

    internal static string Redact(string text, IEnumerable<string> privatePaths) {
        foreach (string path in privatePaths.Where(value => !string.IsNullOrWhiteSpace(value)).OrderByDescending(value => value.Length)) {
            text = text.Replace(path.Replace("\\", "\\\\"), "<path>", StringComparison.OrdinalIgnoreCase)
                .Replace(path, "<path>", StringComparison.OrdinalIgnoreCase).Replace(path.Replace('\\', '/'), "<path>", StringComparison.OrdinalIgnoreCase);
        }
        text = Regex.Replace(text, @"(?i)[A-Z]:[\\/]+Users[\\/]+[^\\/\s\""<>]+", "<user>");
        text = Regex.Replace(text, @"(?:gh[pousr]_[A-Za-z0-9_]{12,}|github_pat_[A-Za-z0-9_]+|(?i:bearer)\s+[A-Za-z0-9._~+/=-]+)", "<redacted>");
        text = Regex.Replace(text, @"(?i)(\""?(?:access[_-]?token|refresh[_-]?token|client[_-]?secret|password|api[_-]?key)\""?\s*[:=]\s*)(\""[^\""\r\n]*\""|[^\s,;]+)", "$1<redacted>");
        return Regex.Replace(text, @"https://[^\s/@]+@", "https://<redacted>@");
    }

    private static string Limit(string text, ref bool truncated) {
        byte[] bytes = Encoding.UTF8.GetBytes(text);
        if (bytes.Length <= MaxLogBytes) return text;
        truncated = true;
        return Encoding.UTF8.GetString(bytes, 0, 65536) + "\n[ログの一部を省略]\n" + Encoding.UTF8.GetString(bytes, bytes.Length - 900000, 900000);
    }
}
