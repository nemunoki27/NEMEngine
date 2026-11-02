using System.IO;
using System.Net.Http;
using System.Text;
using NEMEngine.Distribution;

namespace NEMEngine.Launcher;

// Gitの子プロセスではWindowと単一起動の窓口を作らない
internal static class GitHeadlessCommands {
    public static async Task<int> RunAsync(string[] args) {
        try {
            if (args.Length < 3) throw new ArgumentException("Gitの実行引数が不正です");
            string root = Decode(args[1]);
            if (args[0] == "--git-lfs") {
                using var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
                var tools = await new GitToolchainService(root, client).EnsureAsync(null, CancellationToken.None);
                return await new GitCommandRunner(tools, Environment.ProcessPath!, root).RunLfsPipeAsync(Environment.CurrentDirectory,
                    args.Skip(2), Console.OpenStandardInput(), Console.OpenStandardOutput(), Console.OpenStandardError(), CancellationToken.None);
            }
            if (args.Length != 5 || !long.TryParse(args[2], out long accountID)) throw new ArgumentException("Gitの認証引数が不正です");
            using var input = new StreamReader(Console.OpenStandardInput(), new UTF8Encoding(false));
            var fields = new Dictionary<string, string>(StringComparer.Ordinal);
            for (int index = 0; index < 32; ++index) {
                string? line = await input.ReadLineAsync();
                if (string.IsNullOrEmpty(line)) break;
                if (line.Length > 8192) throw new InvalidDataException("Gitの認証要求が長すぎます");
                int separator = line.IndexOf('=');
                if (separator > 0) fields[line[..separator]] = line[(separator + 1)..];
            }
            if (args[4] is "store" or "erase") return 0;
            if (args[4] != "get" || fields.GetValueOrDefault("protocol") != "https" || fields.GetValueOrDefault("host") != "github.com") return 0;
            string path = fields.GetValueOrDefault("path", "").Trim('/');
            if (path != args[3] && path != args[3] + ".git" && path != args[3] + "/info/lfs" && path != args[3] + ".git/info/lfs") return 0;
            using var api = new HttpClient(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = TimeSpan.FromMinutes(2) };
            var session = new GitHubSession(root, api, DistributionDefaults.GitHub);
            var identity = session.FindAccount(accountID) ?? throw new InvalidOperationException("ランチャーで、このプロジェクトのGitHubアカウントを追加してください");
            string token = await session.GetAccessTokenAsync(accountID, CancellationToken.None);
            // 資格情報はGitの専用パイプだけへ返す
            using var output = new StreamWriter(Console.OpenStandardOutput(), new UTF8Encoding(false));
            await output.WriteAsync("username=" + identity.Login + "\npassword=" + token + "\n\n");
            await output.FlushAsync();
            return 0;
        } catch (Exception error) {
            using var output = new StreamWriter(Console.OpenStandardError(), new UTF8Encoding(false));
            await output.WriteLineAsync(error.Message);
            return 1;
        }
    }

    private static string Decode(string value) => Encoding.UTF8.GetString(Convert.FromBase64String(value));
}
