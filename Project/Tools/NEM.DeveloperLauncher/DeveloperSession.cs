using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine.DeveloperLauncher;

internal sealed class DeveloperSession(string repository) {
    internal ReleaseWorkspace Workspace { get; } = new(repository);
    internal string Root { get; } = repository;
    internal Action<string>? Progress { get; set; }
    internal static string Text(JsonNode? data, string key) => data?[key]?.GetValue<string>() ?? "";
    internal static JsonObject Read(string path) => JsonNode.Parse(File.ReadAllText(path))?.AsObject()
        ?? throw new InvalidDataException("設定ファイルを読み込めません: " + path);

    internal static bool ValidVersion(string value) => Version.TryParse(value, out var v) && v.Build >= 0 && v.Revision < 0 && v.ToString(3) == value;
    internal static void RequireVersion(string value) {
        if (!ValidVersion(value))
            throw new ArgumentException("版番号は 0.1.0 の形式で入力してください");
    }

    internal static async Task<JsonObject> Editor(string? name, JsonObject request) {
        if (string.IsNullOrEmpty(name))
            throw new InvalidOperationException("開発用Editorの「バージョン管理」で編集元を接続してください");
        using var pipe = new NamedPipeClientStream(".", name, PipeDirection.InOut, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
        await pipe.ConnectAsync(1800);
        using var writer = new StreamWriter(pipe, new UTF8Encoding(false), leaveOpen: true) { AutoFlush = true, NewLine = "\n" };
        using var reader = new StreamReader(pipe, Encoding.UTF8, false, 4096, leaveOpen: true);
        await writer.WriteLineAsync(request.ToJsonString());
        using var timeout = Text(request, "action") == "status" ? new CancellationTokenSource(TimeSpan.FromSeconds(3)) : null;
        var line = await reader.ReadLineAsync(timeout?.Token ?? CancellationToken.None) ?? throw new EndOfStreamException("Editorとの接続が終了しました");
        var response = JsonNode.Parse(line)?.AsObject() ?? throw new InvalidDataException("Editorの応答を読み込めません");
        if (response["ok"]?.GetValue<bool>() != true && response["chooseWorkspace"]?.GetValue<bool>() != true)
            throw new InvalidOperationException(Text(response, "message"));
        return response;
    }

    internal async Task<JsonObject> Release(JsonObject request) {
        var output = await Request("engine-release.mjs", request, true);
        return JsonNode.Parse(output)?.AsObject() ?? throw new InvalidDataException("配布サービスの応答を読み込めません");
    }

    private string? contentRequest;
    internal void CancelContent() { if (contentRequest != null) File.WriteAllText(contentRequest + ".cancel", ""); }

    internal Task Content(JsonObject request) {
        request["schemaVersion"] = 1;
        return Request("content.mjs", request, false);
    }

    private async Task<string> Request(string script, JsonObject request, bool result) {
        var jobs = Path.Combine(Root, "Generated/DistributionContent/Jobs");
        Directory.CreateDirectory(jobs);
        var file = Path.Combine(jobs, Guid.NewGuid().ToString("N") + ".json");
        try {
            await File.WriteAllTextAsync(file, request.ToJsonString());
            if (!result)
                contentRequest = file;
            string path = Path.Combine(Root, "Tools/Distribution", script);
            return await Run("node", result ? [path, file] : [path, "--request", file], result);
        } finally { if (!result) { contentRequest = null; File.Delete(file + ".cancel"); } File.Delete(file); }
    }

    internal void DiscardBuild() {
        try { Workspace.Discard(); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) {
            Progress?.Invoke("配布作業フォルダーは次回起動時に削除します　" + error.Message);
        }
    }

    internal async Task<string> Build(string version) {
        RequireVersion(version);
        var output = Workspace.Create();
        try {
            var file = Path.Combine(Root, "Tools/Distribution/PackageEngine.ps1");
            await Run("pwsh", ["-NoLogo", "-NoProfile", "-NonInteractive", "-File", file,
                "-Version", version, "-IsolatedBuild", "-OutputDirectory", output], false);
            var release = Path.Combine(output, version, "release-" + version + ".json");
            if (!File.Exists(release))
                throw new FileNotFoundException("梱包したエンジンが見つかりません", release);
            return release;
        } catch {
            DiscardBuild();
            throw;
        }
    }

    private async Task<string> Run(string program, string[] arguments, bool jsonResult) {
        var info = new ProcessStartInfo(program) {
            WorkingDirectory = Root,
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8,
            StandardErrorEncoding = Encoding.UTF8
        };
        foreach (var arg in arguments)
            info.ArgumentList.Add(arg);
        using var process = Process.Start(info) ?? throw new InvalidOperationException(program + "を起動できません");
        try { Workspace.SetWorker(process); }
        catch (InvalidOperationException) when (process.HasExited) { }
        catch {
            if (!process.HasExited) process.Kill(entireProcessTree: true);
            await process.WaitForExitAsync();
            throw;
        }
        string last = "", error = "";
        async Task ReadLines(StreamReader reader, bool stderr) {
            while (await reader.ReadLineAsync() is { } line) {
                if (string.IsNullOrWhiteSpace(line))
                    continue;
                if (stderr)
                    error = line;
                else
                    last = line;
                if (!jsonResult || stderr || !line.StartsWith('{'))
                    Progress?.Invoke(line);
            }
        }
        await Task.WhenAll(ReadLines(process.StandardOutput, false), ReadLines(process.StandardError, true), process.WaitForExitAsync());
        Workspace.SetWorker(null);
        if (process.ExitCode != 0)
            throw new InvalidOperationException(error.Length > 0 ? error : last.Length > 0 ? last : program + "の実行に失敗しました");
        return last;
    }
}
