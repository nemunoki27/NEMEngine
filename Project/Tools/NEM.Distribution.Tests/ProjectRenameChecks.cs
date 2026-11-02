using System.Text.Json.Nodes;
using NEMEngine.Distribution;

internal static class ProjectRenameChecks {
    public static void Run(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "rename");
        var store = new LauncherStore(Path.Combine(directory, "state"));
        store.State.InstallRoot = Path.Combine(directory, "engines");
        store.SaveSettings();
        ProjectEntry Create(string name) {
            string projectRoot = Path.Combine(directory, "projects", name);
            string descriptor = Path.Combine(projectRoot, name + ".nemproject");
            DistributionStorage.Write(descriptor, new { schemaVersion = 1, projectGuid = Guid.NewGuid().ToString("N"), name, engineVersion = "0.1.3" });
            File.WriteAllText(Path.Combine(projectRoot, name + ".slnx"), "<Solution />");
            Directory.CreateDirectory(Path.Combine(projectRoot, "GameAssets"));
            File.WriteAllText(Path.Combine(projectRoot, "GameAssets/Keep.meta"), "stable asset guid");
            Directory.CreateDirectory(Path.Combine(projectRoot, ".git"));
            File.WriteAllText(Path.Combine(projectRoot, ".git/history.fixture"), "keep history");
            var entry = ProjectService.Read(descriptor) with { LastOpened = DateTime.UtcNow };
            store.Register(entry);
            return entry;
        }
        void Reject(Action action, string message) {
            try { action(); }
            catch (Exception error) when (error is IOException or ArgumentException or UnauthorizedAccessException or OperationCanceledException) { check(true, message); return; }
            check(false, message);
        }
        var service = new ProjectService(store);
        var original = Create("Test");
        var renamed = service.Rename(original, "日本語 NewName");
        string renamedRoot = Path.GetDirectoryName(renamed.Descriptor)!;
        check(renamed.Name == "日本語 NewName" && Path.GetFileName(renamedRoot) == renamed.Name &&
            Path.GetFileName(renamed.Descriptor) == renamed.Name + ".nemproject", "実体の名前が揃っていません");
        check(File.Exists(Path.Combine(renamedRoot, renamed.Name + ".slnx")) && !Directory.Exists(Path.GetDirectoryName(original.Descriptor)), "旧名の実体が残っています");
        check(renamed.ProjectGUID == original.ProjectGUID && renamed.LastOpened == original.LastOpened, "改名で識別子か最終起動時刻が変わりました");
        check(File.ReadAllText(Path.Combine(renamedRoot, "GameAssets/Keep.meta")) == "stable asset guid" &&
            File.ReadAllText(Path.Combine(renamedRoot, ".git/history.fixture")) == "keep history", "AssetかGit履歴を変更しました");
        check(store.State.Projects.Single().Descriptor == renamed.Descriptor, "旧名の一覧登録を置換していません");
        var lower = Create("CaseName");
        var changedCase = service.Rename(lower, "casename");
        check(Path.GetFileName(Directory.GetFiles(Path.GetDirectoryName(changedCase.Descriptor)!, "*.nemproject").Single()) == "casename.nemproject",
            "大小文字だけの改名に失敗しました");
        Reject(() => service.Rename(changedCase, "日本語 NewName"), "保存先の衝突を受理しました");
        foreach (string name in new[] { "", "CON", "../escape", "a:b", "trailing.", "trailing " })
            Reject(() => service.Rename(changedCase, name), "不正な名前を受理しました: " + name);
        using (var cancel = new CancellationTokenSource()) {
            cancel.Cancel();
            Reject(() => service.Rename(changedCase, "Canceled", cancel.Token), "キャンセル後に改名しました");
        }
        using (File.Open(changedCase.Descriptor, FileMode.Open, FileAccess.Read, FileShare.Read))
            Reject(() => service.Rename(changedCase, "Locked"), "使用中のプロジェクトを改名しました");
        check(File.Exists(changedCase.Descriptor) && !Directory.Exists(Path.Combine(directory, "projects/Locked")), "使用中の拒否で旧配置を失いました");
        string state = Path.Combine(store.Root, "state.json");
        File.SetAttributes(state, FileAttributes.ReadOnly);
        try { Reject(() => service.Rename(changedCase, "Rollback"), "保存失敗を受理しました"); }
        finally { File.SetAttributes(state, FileAttributes.Normal); }
        check(File.Exists(changedCase.Descriptor) && ProjectService.Read(changedCase.Descriptor).Name == changedCase.Name &&
            !Directory.Exists(Path.Combine(directory, "projects/Rollback")), "改名失敗から旧状態へ戻せません");

        // チームから取得したファイル名変更を同じGUIDへ対応付ける
        string peer = Path.Combine(renamedRoot, "TeamRenamed.nemproject");
        File.Move(renamed.Descriptor, peer);
        var data = JsonNode.Parse(File.ReadAllText(peer))!.AsObject();
        data["name"] = "TeamRenamed";
        DistributionStorage.Write(peer, data);
        store.RefreshProjects();
        check(store.State.Projects.Single(item => item.ProjectGUID == renamed.ProjectGUID).Descriptor == peer,
            "チーム側の改名へ登録を追従できません");
        store.Register(ProjectService.Read(peer) with { ProjectGUID = "", LastOpened = original.LastOpened });
        store.RefreshProjects();
        check(store.State.Projects.Single(item => item.Descriptor == peer).ProjectGUID == original.ProjectGUID, "旧登録のGUIDを補完できません");
        check(!Directory.EnumerateDirectories(Path.Combine(directory, "projects"), ".project-rename-*").Any(), "改名用フォルダーが残っています");
    }
}
