using System.Diagnostics;
using System.Text;
using System.Text.Json;

namespace NEMEngine.ProjectTool;

internal sealed record VisualStudioInstallation(string Executable, Version Version, bool IsPrerelease) {
    internal string Interop => Path.Combine(Path.GetDirectoryName(Executable)!, "PublicAssemblies/Microsoft.VisualStudio.Interop.dll");
}

// 配置名ではなくInstallerの登録情報から使用可能なIDEを選ぶ
internal static class VisualStudioDiscovery {
    internal static readonly string[] Arguments = ["-all", "-prerelease", "-products", "*", "-format", "json", "-utf8", "-include", "packages"];

    internal static IReadOnlyList<VisualStudioInstallation> Find() {
        string finder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86), "Microsoft Visual Studio/Installer/vswhere.exe");
        if (!File.Exists(finder)) throw new IOException("Visual Studio Installerが見つかりません Visual Studio 2026以降と.NETデスクトップ開発を導入してください");
        var query = new ProcessStartInfo(finder) { RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8, UseShellExecute = false, CreateNoWindow = true };
        foreach (string argument in Arguments) query.ArgumentList.Add(argument);
        using var search = Process.Start(query) ?? throw new IOException("Visual Studioの検索を開始できません");
        Task<string> output = search.StandardOutput.ReadToEndAsync();
        Task<string> error = search.StandardError.ReadToEndAsync();
        if (!search.WaitForExit(10000)) {
            search.Kill(true);
            search.WaitForExit();
            throw new IOException("Visual Studioの検索が時間内に完了しません Visual Studio Installerの状態を確認してください");
        }
        if (search.ExitCode != 0) throw new IOException("Visual Studioの検索に失敗しました: " + error.GetAwaiter().GetResult().Trim());
        try { return Read(output.GetAwaiter().GetResult(), File.Exists); }
        catch (JsonException exception) { throw new IOException("Visual Studioの登録情報を読み込めません Visual Studio Installerを修復してください", exception); }
    }

    internal static IReadOnlyList<VisualStudioInstallation> Read(string json, Func<string, bool> exists) {
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Array) throw new JsonException("IDE一覧の形式が不正です");
        var found = new List<VisualStudioInstallation>();
        var failures = new HashSet<string>();
        bool installed = false, supported = false;
        foreach (var entry in document.RootElement.EnumerateArray()) {
            string value = Text(entry, "productPath");
            if (!Path.GetFileName(value).Equals("devenv.exe", StringComparison.OrdinalIgnoreCase)) continue;
            installed = true;
            if (!Version.TryParse(Text(entry, "installationVersion"), out var version) || version.Major < 18) continue;
            supported = true;
            if (!Flag(entry, "isComplete") || !Flag(entry, "isLaunchable") || Flag(entry, "isRebootRequired")) {
                failures.Add("インストール未完了または再起動が必要");
                continue;
            }
            if (!exists(value)) { failures.Add("IDEの実行ファイルが欠損"); continue; }
            if (!entry.TryGetProperty("packages", out var packages) || packages.ValueKind != JsonValueKind.Array ||
                !packages.EnumerateArray().Any(package => Text(package, "id") == "Microsoft.VisualStudio.Component.Roslyn.LanguageServices")) {
                failures.Add("C#言語機能が未導入");
                continue;
            }
            var candidate = new VisualStudioInstallation(Path.GetFullPath(value), version, Flag(entry, "isPrerelease"));
            if (exists(candidate.Interop)) found.Add(candidate);
            else failures.Add("Interop DLLが欠損");
        }
        if (found.Count == 0) throw new IOException(!installed ? "Visual StudioのIDEが見つかりません Visual Studio 2026以降を導入してください" :
            !supported ? "このプロジェクトにはVisual Studio 2026以降が必要です" :
            "使用可能なVisual Studioがありません: " + string.Join(" / ", failures) + "\nVisual Studio Installerで.NETデスクトップ開発の追加、修復、再起動の必要性を確認してください");
        return found.OrderBy(item => item.IsPrerelease).ThenByDescending(item => item.Version).ThenBy(item => item.Executable, StringComparer.OrdinalIgnoreCase).ToArray();
    }

    internal static VisualStudioInstallation Select(IReadOnlyList<VisualStudioInstallation> installations, string? executable = null) {
        if (executable == null) return installations[0];
        string target = Path.GetFullPath(executable);
        return installations.FirstOrDefault(item => item.Executable.Equals(target, StringComparison.OrdinalIgnoreCase))
            ?? throw new IOException("このSolutionを開いているVisual Studioは対応条件を満たしていません Visual Studio 2026以降のC#開発環境で開き直してください");
    }

    private static string Text(JsonElement value, string key) => value.ValueKind == JsonValueKind.Object && value.TryGetProperty(key, out var property) && property.ValueKind == JsonValueKind.String ? property.GetString()! : "";
    private static bool Flag(JsonElement value, string key) => value.ValueKind == JsonValueKind.Object && value.TryGetProperty(key, out var property) && property.ValueKind == JsonValueKind.True;
}
