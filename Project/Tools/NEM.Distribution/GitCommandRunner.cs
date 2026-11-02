using System.Diagnostics;
using System.Text;
using System.Text.RegularExpressions;

namespace NEMEngine.Distribution;

public sealed record GitCommandResult(int ExitCode, string Output, string Error);

// Gitの引数と入出力をshellへ連結しない
public sealed class GitCommandRunner(GitToolchain tools, string launcher, string stateRoot, string? credentialRoot = null) {
    public GitToolchain Tools => tools;
    public string LfsCommand => ShellQuote(launcher) + " --git-lfs " + ShellQuote(Encode(stateRoot));
    public string CredentialCommand(string fullName, long accountID) => "!" + ShellQuote(launcher) +
        " --git-credential " + ShellQuote(Encode(credentialRoot ?? stateRoot)) + " " + accountID + " " + ShellQuote(fullName);

    public async Task<GitCommandResult> RunAsync(string directory, IEnumerable<string> arguments, CancellationToken token,
        string? input = null, bool allowFailure = false, string? fullName = null, long accountID = 0) {
        var start = CreateStart(tools.Git, directory);
        start.ArgumentList.Add("--no-optional-locks");
        start.ArgumentList.Add("-c"); start.ArgumentList.Add("core.quotepath=false");
        start.ArgumentList.Add("-c"); start.ArgumentList.Add("core.longpaths=true");
        if (fullName != null) {
            start.ArgumentList.Add("-c"); start.ArgumentList.Add("credential.helper=");
            start.ArgumentList.Add("-c"); start.ArgumentList.Add("credential.helper=" + CredentialCommand(fullName, accountID));
            start.ArgumentList.Add("-c"); start.ArgumentList.Add("credential.useHttpPath=true");
        }
        foreach (string argument in arguments) start.ArgumentList.Add(argument);
        start.Environment["GIT_LFS_SKIP_SMUDGE"] = "1";
        using var process = Process.Start(start) ?? throw new IOException("Gitを起動できません");
        var output = process.StandardOutput.ReadToEndAsync(token);
        var error = process.StandardError.ReadToEndAsync(token);
        try {
            if (input != null) await process.StandardInput.WriteAsync(input.AsMemory(), token);
            process.StandardInput.Close();
            await process.WaitForExitAsync(token);
            var result = new GitCommandResult(process.ExitCode, await output, await error);
            if (result.ExitCode != 0 && !allowFailure)
                throw new IOException("Gitの処理に失敗しました\n" + CleanError(result.Error));
            return result;
        } catch (OperationCanceledException) {
            if (!process.HasExited) process.Kill(true);
            await process.WaitForExitAsync(CancellationToken.None);
            throw;
        }
    }

    public async Task<int> RunLfsPipeAsync(string directory, IEnumerable<string> arguments, Stream input, Stream output, Stream error,
        CancellationToken token) {
        var start = CreateStart(tools.LFS, directory);
        foreach (string argument in arguments) start.ArgumentList.Add(argument);
        using var process = Process.Start(start) ?? throw new IOException("Git LFSを起動できません");
        var incoming = Task.Run(async () => {
            try { await input.CopyToAsync(process.StandardInput.BaseStream, token); }
            finally { process.StandardInput.Close(); }
        }, token);
        var outgoing = process.StandardOutput.BaseStream.CopyToAsync(output, token);
        var errors = process.StandardError.BaseStream.CopyToAsync(error, token);
        try {
            await Task.WhenAll(incoming, outgoing, errors, process.WaitForExitAsync(token));
            return process.ExitCode;
        } catch (OperationCanceledException) {
            if (!process.HasExited) process.Kill(true);
            await process.WaitForExitAsync(CancellationToken.None);
            throw;
        }
    }

    private ProcessStartInfo CreateStart(string executable, string directory) {
        var start = new ProcessStartInfo(executable) { WorkingDirectory = directory, UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardInput = true, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = new UTF8Encoding(false), StandardErrorEncoding = new UTF8Encoding(false) };
        start.Environment["PATH"] = tools.PathPrefix + Path.PathSeparator + Environment.GetEnvironmentVariable("PATH");
        start.Environment["GIT_TERMINAL_PROMPT"] = "0";
        start.Environment["GCM_INTERACTIVE"] = "Never";
        start.Environment.Remove("GIT_DIR");
        start.Environment.Remove("GIT_WORK_TREE");
        start.Environment.Remove("GIT_INDEX_FILE");
        return start;
    }

    internal static string Encode(string value) => Convert.ToBase64String(Encoding.UTF8.GetBytes(value));
    internal static string Decode(string value) => Encoding.UTF8.GetString(Convert.FromBase64String(value));
    internal static string ShellQuote(string value) => "'" + value.Replace('\\', '/').Replace("'", "'\\''") + "'";
    private static string CleanError(string value) {
        string cleaned = Regex.Replace(value, @"https://[^\s/@]+@", "https://***@");
        return cleaned.Length > 2000 ? cleaned[..2000] : cleaned;
    }
}
