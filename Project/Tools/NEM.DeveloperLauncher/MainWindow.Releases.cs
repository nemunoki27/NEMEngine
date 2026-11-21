using System.IO;
using System.Text.Json.Nodes;
using System.Windows;
using static NEMEngine.DeveloperLauncher.DeveloperSession;

namespace NEMEngine.DeveloperLauncher;

public partial class MainWindow {
    private string? preparedVersion, preparedRelease;

    private JsonObject ReleaseRequest(string action, string version) {
        if (catalog == null)
            throw new InvalidOperationException("配布一覧を更新してから操作してください");
        return new JsonObject { ["action"] = action, ["version"] = version, ["etag"] = catalog["etag"]?.DeepClone() };
    }

    private string LatestVersion() => Entries("engines").Select(x => Version.Parse(Text(x, "version")))
        .OrderDescending().FirstOrDefault()?.ToString(3) ?? "";

    private async Task PublishRelease() {
        if (catalog == null)
            await LoadCatalog();
        var used = Entries("engines").Select(x => Text(x, "version")).Concat(Strings("retiredVersions")).ToHashSet();
        var latest = Version.TryParse(LatestVersion(), out var version) ? version : new Version(0, 1, 0);
        var patch = latest.Build + 1;
        string next = used.Count == 0 ? "0.1.0" : $"{latest.Major}.{latest.Minor}.{patch}";
        while (used.Contains(next))
            next = $"{latest.Major}.{latest.Minor}.{++patch}";
        var dialog = new EditDialog(this, "新しいバージョンを配布", "公開");
        dialog.Input("version", "版番号", preparedVersion != null && !used.Contains(preparedVersion) ? preparedVersion : next);
        dialog.Input("notes", "更新内容", "", true);
        if (!dialog.Accept(() => {
            var requested = dialog.Value("version");
            RequireVersion(requested);
            if (used.Contains(requested))
                throw new ArgumentException("公開済み、または削除済みの版番号です");
            if (dialog.Value("notes").Length == 0)
                throw new ArgumentException("更新内容を入力してください");
        }))
            Cancel();
        var number = dialog.Value("version");
        var request = ReleaseRequest("publish", number);
        request["notes"] = dialog.Value("notes");
        bool reuse = false;
        if (preparedVersion == number && File.Exists(preparedRelease)) {
            var choice = MessageBox.Show(this, number + " の前回の梱包結果を使用しますか？\n\nはい：前回の配布物を使用\nいいえ：ビルドし直す",
                "配布物の再試行", MessageBoxButton.YesNoCancel, MessageBoxImage.Question, MessageBoxResult.Cancel);
            if (choice == MessageBoxResult.Cancel) Cancel();
            reuse = choice == MessageBoxResult.Yes;
        }
        if (!reuse) {
            AppendLog(number + " をビルド・梱包しています");
            preparedRelease = null;
            preparedVersion = null;
            preparedRelease = await session.Build(number);
            preparedVersion = number;
        }
        request["releasePath"] = preparedRelease;
        AppendLog("配布物を検証してアップロードしています");
        await ExecuteRelease(request);
        session.DiscardBuild();
        preparedRelease = null;
        preparedVersion = null;
    }

    private async Task ChangeRelease(string command) {
        if (Engines.SelectedItem is not ReleaseRow row)
            throw new InvalidOperationException("一覧から版を選択してください");
        if ((command == "CleanupRelease") != row.Pending)
            throw new InvalidOperationException(row.Pending ? "本体ZIPの削除を再試行してください" : "この版の本体ZIPは削除待ちではありません");
        var request = ReleaseRequest(command switch { "EditNotes" => "notes", "CleanupRelease" => "cleanup", _ => "delete" }, row.Version);
        if (command == "EditNotes") {
            var dialog = new EditDialog(this, row.Version + " の更新内容", "保存");
            dialog.Input("notes", "更新内容", row.Notes, true);
            if (!dialog.Accept(() => { if (dialog.Value("notes").Length == 0) throw new ArgumentException("更新内容を入力してください"); }))
                Cancel();
            request["notes"] = dialog.Value("notes");
        } else {
            var dependencies = Entries("samples").Where(x => Text(x, "engineVersion") == row.Version).Select(x => Text(x, "name")).Distinct().ToArray();
            if (dependencies.Length != 0)
                throw new InvalidOperationException("この版を使用する公開サンプルがあります　" + string.Join("、", dependencies));
            if (!Confirm(this, "エンジンを削除", row.Version + " の配布用ZIPを削除します。この版は再インストールできなくなります。"))
                Cancel();
        }
        await ExecuteRelease(request);
    }

    private async Task ExecuteRelease(JsonObject request) {
        try { ApplyCatalog(await session.Release(request)); } catch {
            try { await LoadCatalog(); } catch (Exception refreshError) { AppendLog("配布一覧の再取得に失敗しました　" + refreshError.Message); }
            throw;
        }
    }
}
