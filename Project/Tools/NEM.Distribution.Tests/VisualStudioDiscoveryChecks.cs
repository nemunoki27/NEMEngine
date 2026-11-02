using System.Text.Json;
using NEMEngine.ProjectTool;

internal static class VisualStudioDiscoveryChecks {
    public static void Run(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "IDE検出 空白");
        var stable = new VisualStudioInstallation(Path.GetFullPath(Path.Combine(directory, "正式版/Common7/IDE/devenv.exe")), new(18, 9), false);
        var latest = new VisualStudioInstallation(Path.GetFullPath(Path.Combine(directory, "Professional/Common7/IDE/devenv.exe")), new(18, 10), false);
        var insider = new VisualStudioInstallation(Path.GetFullPath(Path.Combine(directory, "Insiders/Common7/IDE/devenv.exe")), new(18, 11), true);
        var future = new VisualStudioInstallation(Path.GetFullPath(Path.Combine(directory, "将来版/Common7/IDE/devenv.exe")), new(19, 0), false);
        var files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var candidate in new[] { stable, latest, insider, future }) { files.Add(candidate.Executable); files.Add(candidate.Interop); }
        object Entry(VisualStudioInstallation item, bool complete = true, bool launchable = true, bool csharp = true, bool reboot = false) => new {
            productPath = item.Executable, installationVersion = item.Version.ToString(), isPrerelease = item.IsPrerelease, isComplete = complete,
            isLaunchable = launchable, isRebootRequired = reboot, packages = csharp ? new[] { new { id = "Microsoft.VisualStudio.Component.Roslyn.LanguageServices" } } : []
        };
        IReadOnlyList<VisualStudioInstallation> Read(params object[] entries) => VisualStudioDiscovery.Read(JsonSerializer.Serialize(entries), files.Contains);
        var all = Read(Entry(insider), Entry(stable), Entry(latest));
        check(all.Count == 3 && VisualStudioDiscovery.Select(all) == latest, "正式版の最新を優先していません");
        check(VisualStudioDiscovery.Select(Read(Entry(insider))) == insider, "Insidersのみの環境で選択できません");
        check(VisualStudioDiscovery.Select(all, insider.Executable.ToUpperInvariant()) == insider, "起動済みInsidersのSolutionを優先できません");
        check(VisualStudioDiscovery.Select(all, insider.Executable).Interop == insider.Interop, "接続先とは別のInteropを選びました");
        check(VisualStudioDiscovery.Select(Read(Entry(future))) == future, "対応範囲を年号で制限しています");
        files.Remove(latest.Executable);
        check(VisualStudioDiscovery.Select(Read(Entry(latest), Entry(stable), Entry(insider))) == stable, "欠損した最新版で候補検索を終了しました");
        files.Add(latest.Executable);
        files.Remove(latest.Interop);
        check(VisualStudioDiscovery.Select(Read(Entry(latest), Entry(insider))) == insider, "Interop欠損のIDEを選択しました");
        files.Add(latest.Interop);
        check(VisualStudioDiscovery.Select(Read(Entry(latest, csharp: false), Entry(insider))) == insider, "C#機能のない正式版を選択しました");
        foreach (object value in new[] { Entry(latest, complete: false), Entry(latest, launchable: false), Entry(latest, reboot: true),
            Entry(stable with { Version = new(17, 14) }), Entry(stable with { Executable = "C:/Tools/MSBuild.exe" }) }) {
            try { Read(value); throw new Exception("使用できないIDEを選択しました"); } catch (IOException) { check(true, "不適格なIDEを除外"); }
        }
        try { Read(); throw new Exception("IDE未導入を正常としました"); } catch (IOException) { check(true, "未導入"); }
        try { VisualStudioDiscovery.Select(all, Path.Combine(directory, "未登録/devenv.exe")); throw new Exception("別のIDEへ接続しました"); } catch (IOException) { check(true, "起動済みIDEの不一致"); }
        try { VisualStudioDiscovery.Read("{}", files.Contains); throw new Exception("壊れた一覧を受理しました"); } catch (JsonException) { check(true, "一覧の形式"); }
        check(VisualStudioDiscovery.Arguments.Contains("-prerelease") && VisualStudioDiscovery.Arguments.Contains("-utf8") &&
            VisualStudioDiscovery.Arguments.Contains("packages") && !VisualStudioDiscovery.Arguments.Contains("-latest"), "候補を列挙する検索条件が不足しています");
        Console.WriteLine("PASS Visual Studio discovery / Stable・Insiders・欠損・C#機能・複数版");
    }
}
