using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows;
using static NEMEngine.DeveloperLauncher.DeveloperSession;

namespace NEMEngine.DeveloperLauncher;

public partial class MainWindow {
    private async Task SaveScene(bool sample) {
        string? pipe = editorPipe;
        var status = await Editor(pipe, new JsonObject { ["action"] = "status" });
        if (status["canSave"]?.GetValue<bool>() != true)
            throw new InvalidOperationException(Text(status, "message"));
        string workspace = Text(status, "workspaceId");
        var entry = sample ? store.List("Sample").FirstOrDefault(s => s.Id == workspace) : Selected(Templates);
        string kind = sample ? "Sample" : entry!.Id;
        var dialog = new EditDialog(this, sample ? "現在のシーンをサンプルとして保存" : kind + "テンプレートを保存", "保存");
        dialog.Label(Text(status, "project") + "   /   " + Text(status, "scene"));
        dialog.Input("name", "名前", entry?.Name ?? Text(status, "scene"));
        dialog.Input("description", "説明", Text(entry?.Data, "description"), true);
        if (sample) {
            dialog.Input("version", "版番号", entry == null ? "1.0.0" : Text(entry.Data, "version"));
            dialog.Input("engine", "対応エンジン版", entry == null ? LatestVersion() : Text(entry.Data, "engineVersion"));
        }
        var extras = entry?.Data["additionalPaths"]?.AsArray().Select(p => p!.GetValue<string>()) ?? [];
        string root = Text(status, "projectRoot");
        dialog.Files(root, extras);
        if (!dialog.Accept(() => {
            if (dialog.Value("name").Length == 0)
                throw new ArgumentException("名前を入力してください");
            if (sample) { RequireVersion(dialog.Value("version")); RequireVersion(dialog.Value("engine")); }
            foreach (var value in dialog.Paths()) {
                var relative = Path.GetRelativePath(root, Path.GetFullPath(value, root));
                if (Path.IsPathRooted(value) || relative == "." || relative == ".." || relative.StartsWith(".." + Path.DirectorySeparatorChar))
                    throw new ArgumentException("追加パスは保存元プロジェクト内のファイルまたはフォルダーを指定してください");
            }
        }))
            Cancel();
        var request = new JsonObject {
            ["action"] = "save",
            ["kind"] = kind,
            ["id"] = entry?.Id ?? "",
            ["sceneId"] = Text(status, "sceneId"),
            ["name"] = dialog.Value("name"),
            ["description"] = dialog.Value("description"),
            ["additionalPaths"] = JsonSerializer.SerializeToNode(dialog.Paths())
        };
        if (sample) { request["version"] = dialog.Value("version"); request["engineVersion"] = dialog.Value("engine"); }
        AppendLog("シーンと依存アセットを保存しています");
        var result = await Editor(pipe, request);
        RefreshSources(Text(result, "id"));
    }

    private async Task OpenSource(SourceEntry entry) {
        var pipe = editorPipe;
        var request = new JsonObject { ["action"] = "open", ["id"] = entry.Id };
        var result = await Editor(pipe, request);
        if (result["chooseWorkspace"]?.GetValue<bool>() == true) {
            var choice = MessageBox.Show(this, Text(result, "message") + "\n\nはい：保存元から作り直す\nいいえ：前回の作業を再開する",
                "編集用プロジェクト", MessageBoxButton.YesNoCancel, MessageBoxImage.Question, MessageBoxResult.Cancel);
            if (choice == MessageBoxResult.Cancel)
                Cancel();
            request["recreate"] = choice == MessageBoxResult.Yes;
            await Editor(pipe, request);
        }
    }

    private void EditSample(SourceEntry entry) {
        var dialog = new EditDialog(this, "サンプルの設定", "保存");
        dialog.Input("name", "名前", entry.Name);
        dialog.Input("description", "説明", Text(entry.Data, "description"), true);
        dialog.Input("version", "版番号", Text(entry.Data, "version"));
        dialog.Input("engine", "対応エンジン版", Text(entry.Data, "engineVersion"));
        if (!dialog.Accept(() => store.SaveSample(entry, dialog.Value("name"), dialog.Value("description"), dialog.Value("version"), dialog.Value("engine"))))
            Cancel();
        RefreshSources(entry.Id);
    }

    private async Task EditAsset(SourceEntry? entry) {
        var dialog = new EditDialog(this, entry == null ? "アセットを追加" : "アセットの設定", "保存");
        dialog.Input("name", "名前", entry?.Name ?? "");
        dialog.Input("description", "説明", Text(entry?.Data, "description"), true);
        dialog.Input("version", "版番号", entry == null ? "1.0.0" : Text(entry.Data, "version"));
        dialog.Input("engine", "推奨エンジン版", entry == null ? LatestVersion() : Text(entry.Data, "recommendedEngineVersion"));
        dialog.Zip(entry == null ? "" : Path.GetFullPath(Text(entry.Data, "archivePath"), entry.Directory));
        if (!dialog.Accept(() => {
            if (dialog.Value("name").Length == 0)
                throw new ArgumentException("名前を入力してください");
            RequireVersion(dialog.Value("version"));
            if (dialog.Value("engine").Length != 0)
                RequireVersion(dialog.Value("engine"));
            if (!File.Exists(dialog.Value("zip")))
                throw new FileNotFoundException("配布するZIPを選択してください");
        }))
            Cancel();
        var saved = await store.SaveAsset(entry?.Id ?? Guid.NewGuid().ToString("N"), dialog.Value("name"), dialog.Value("description"),
            dialog.Value("version"), dialog.Value("engine"), dialog.Value("zip"));
        RefreshSources(saved.Id);
    }

    private async Task ChangeContent(string command) {
        bool sample = command.EndsWith("Sample", StringComparison.Ordinal);
        var entry = Selected(sample ? Samples : Assets);
        bool publish = command.StartsWith("Publish", StringComparison.Ordinal);
        bool delete = command == "DeleteSample";
        string operation = publish ? "公開" : delete ? "削除" : "公開を停止";
        string message = publish ? $"「{entry.Name}」の {Text(entry.Data, "version")} を公開します。" :
            $"「{entry.Name}」のすべての公開版を取り下げます。" + (delete ? "ローカルのサンプル保存元も削除します。" : "");
        if (!Confirm(this, operation, message))
            Cancel();
        var request = new JsonObject { ["action"] = publish ? "publish" : "withdraw", ["kind"] = sample ? "sample" : "asset", ["id"] = entry.Id };
        if (publish) {
            request["name"] = entry.Name;
            request["description"] = Text(entry.Data, "description");
            request["version"] = Text(entry.Data, "version");
            if (sample) { request["engineVersion"] = Text(entry.Data, "engineVersion"); request["sourceDirectory"] = entry.Directory; } else {
                request["recommendedEngineVersion"] = Text(entry.Data, "recommendedEngineVersion");
                request["archivePath"] = Path.GetFullPath(Text(entry.Data, "archivePath"), entry.Directory);
                request["fileName"] = Text(entry.Data, "fileName");
            }
        }
        AppendLog(entry.Name + " の配布情報を更新しています");
        CancelContent.Visibility = Visibility.Visible;
        CancelContent.IsEnabled = true;
        try { await session.Content(request); } finally { CancelContent.Visibility = Visibility.Collapsed; }
        if (delete)
            store.RemoveSample(entry);
        RefreshSources(entry.Id);
        await LoadCatalog();
    }

    private void CancelContentClick(object sender, RoutedEventArgs e) {
        try { session.CancelContent(); CancelContent.IsEnabled = false; Status.Text = "キャンセルを要求しました"; } catch (IOException error) { AppendLog(error.Message); }
    }
}
