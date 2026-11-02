using System.Text.RegularExpressions;

namespace NEMEngine.Distribution;

// 一覧表示では認証とGitの準備を行わない
public static class ProjectGitLink {
    public static string Normalize(string value) {
        value = value.Trim();
        if (value.StartsWith("git@github.com:", StringComparison.OrdinalIgnoreCase)) value = "https://github.com/" + value[15..];
        else if (Uri.TryCreate(value, UriKind.Absolute, out var ssh) && ssh.Scheme == "ssh" &&
            ssh.Host is "github.com" or "ssh.github.com" && ssh.UserInfo == "git" && ssh.Query.Length == 0 && ssh.Fragment.Length == 0)
            value = "https://github.com" + ssh.AbsolutePath;
        if (Uri.TryCreate(value, UriKind.Absolute, out var uri) && uri.Scheme == "https" && uri.Host == "github.com" && uri.Port == 443) {
            var safe = new UriBuilder(uri) { UserName = "", Password = "" };
            try { return "https://github.com/" + GitHubRepositories.ParseURL(safe.Uri.AbsoluteUri); }
            catch (ArgumentException) { }
        }
        return "";
    }

    public static async Task<string> ReadAsync(ProjectEntry project, string storeRoot, GitCommandRunner? runner, CancellationToken token) {
        try {
            string root = Path.GetDirectoryName(Path.GetFullPath(project.Descriptor))!;
            var reset = GitProjectReset.Read(storeRoot, root);
            if (reset?.Binding.RemoteDeleted == true) return "";
            string marker = Path.Combine(root, ".git");
            if (!File.Exists(marker) && !Directory.Exists(marker)) return "";
            if (runner != null) {
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(token);
                timeout.CancelAfter(TimeSpan.FromSeconds(3));
                var found = await runner.RunAsync(root, ["rev-parse", "--show-toplevel"], timeout.Token, allowFailure: true);
                if (found.ExitCode != 0 || !Path.TrimEndingDirectorySeparator(Path.GetFullPath(found.Output.Trim())).Equals(root, StringComparison.OrdinalIgnoreCase)) return "";
                var remote = await runner.RunAsync(root, ["remote", "get-url", "origin"], timeout.Token, allowFailure: true);
                if (remote.ExitCode == 0) return Normalize(remote.Output);
            } else {
                string gitDirectory = Directory.Exists(marker) ? marker : ResolveGitDirectory(marker, root);
                string common = Path.Combine(gitDirectory, "commondir");
                if (File.Exists(common)) gitDirectory = Path.GetFullPath(Path.Combine(gitDirectory, ReadSmall(common).Trim()));
                bool origin = false;
                foreach (string line in ReadSmall(Path.Combine(gitDirectory, "config")).Split('\n')) {
                    string text = line.Trim();
                    if (text.StartsWith('[')) origin = Regex.IsMatch(text, "^\\[remote\\s+\"origin\"\\]$", RegexOptions.IgnoreCase);
                    else if (origin) {
                        var match = Regex.Match(text, "^url\\s*=\\s*(?:\"([^\"]+)\"|([^\\s#;]+))", RegexOptions.IgnoreCase);
                        if (match.Success) return Normalize(match.Groups[1].Success ? match.Groups[1].Value : match.Groups[2].Value);
                    }
                }
            }
            var binding = GitProjectBinding.Read(root);
            return binding is { UploadComplete: false, RemoteDeleted: false, Repository: not null } ? Normalize(binding.Repository.WebURL) : "";
        } catch (Exception error) when (error is IOException or UnauthorizedAccessException or ArgumentException or InvalidDataException or System.Text.Json.JsonException or System.ComponentModel.Win32Exception) { return ""; }
        catch (OperationCanceledException) when (!token.IsCancellationRequested) { return ""; }
    }

    private static string ResolveGitDirectory(string marker, string root) {
        string value = ReadSmall(marker).Trim();
        if (!value.StartsWith("gitdir: ", StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Git管理情報を読み込めません");
        return Path.GetFullPath(Path.Combine(root, value[8..].Trim()));
    }

    private static string ReadSmall(string path) => new FileInfo(path).Length <= 1048576 ? File.ReadAllText(path) : throw new InvalidDataException("Git設定が大きすぎます");
}
