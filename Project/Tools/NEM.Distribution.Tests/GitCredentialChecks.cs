using System.Diagnostics;
using System.Text;
using NEMEngine.Distribution;

internal static class GitCredentialChecks {
    public static async Task RunAsync(string repository, string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "CredentialState");
        long first = 9000000000000 + Random.Shared.Next(1000000), second = first + 1000000;
        var accounts = new GitHubAccounts { ActiveAccountID = second, Accounts = [new(first, "first-fixture", "First"), new(second, "second-fixture", "Second")] };
        DistributionStorage.Write(Path.Combine(directory, "github-session.json"), accounts);
        var vault = new WindowsCredentialStore();
        string key = DistributionDefaults.GitHub.ClientID + "/";
        try {
            vault.Write(key + first, new("synthetic-first-token", "", DateTimeOffset.MaxValue));
            vault.Write(key + second, new("synthetic-second-token", "", DateTimeOffset.MaxValue));
            async Task<string> Request(long id, string path) {
                var start = new ProcessStartInfo(GitNativeChecks.Launcher(repository)) { WorkingDirectory = directory, UseShellExecute = false,
                    RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true };
                foreach (string argument in new[] { "--git-credential", Convert.ToBase64String(Encoding.UTF8.GetBytes(directory)), id.ToString(), "fixture/project", "get" })
                    start.ArgumentList.Add(argument);
                using var process = Process.Start(start)!;
                var output = process.StandardOutput.ReadToEndAsync();
                var error = process.StandardError.ReadToEndAsync();
                await process.StandardInput.WriteAsync("protocol=https\nhost=github.com\npath=" + path + "\n\n");
                process.StandardInput.Close();
                await process.WaitForExitAsync();
                check(process.ExitCode == 0, "Gitの資格情報取得が失敗しました: " + await error);
                return await output;
            }
            string firstOutput = await Request(first, "fixture/project.git");
            check(firstOutput.Contains("username=first-fixture") && firstOutput.Contains("password=synthetic-first-token") && !firstOutput.Contains("second"),
                "Visual Studio用の認証が選択中の別アカウントへ切り替わりました");
            check((await Request(second, "fixture/project.git")).Contains("synthetic-second-token"), "2つ目の資格情報を取得できません");
            check(await Request(first, "other/project.git") == "", "別リポジトリへ資格情報を渡しました");
        } finally { vault.Delete(key + first); vault.Delete(key + second); }
    }
}
