using System.Diagnostics;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using NEMEngine.Distribution;

internal static class ProjectDeletionChecks {
    public static void Run(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "deletion");
        var store = new LauncherStore(Path.Combine(directory, "state"));
        store.State.InstallRoot = Path.Combine(directory, "install");
        store.SaveSettings();
        var service = new ProjectService(store);
        ProjectEntry Create(string name) {
            string descriptor = Path.Combine(directory, "projects", name, Path.GetFileName(name) + ".nemproject");
            DistributionStorage.Write(descriptor, new { schemaVersion = 1, projectGuid = Guid.NewGuid().ToString("N"), name, engineVersion = "0.1.3" });
            var entry = ProjectService.Read(descriptor);
            store.Register(entry);
            return entry;
        }
        void Reject(Action action, string message) {
            try { action(); }
            catch (Exception error) when (error is IOException or ArgumentException or OperationCanceledException or System.Text.Json.JsonException) {
                check(true, message);
                return;
            }
            check(false, message);
        }

        var project = Create("Delete");
        var retained = Create("Retained");
        string projectRoot = Path.GetDirectoryName(project.Descriptor)!;
        File.WriteAllText(Path.Combine(projectRoot, "asset.txt"), "keep until recycled");
        service.Delete(project, target => {
            check(target == projectRoot && store.State.Projects.Contains(project), "移動完了前に登録を解除しました");
            Directory.Move(target, Path.Combine(directory, "simulated-recycle"));
        });
        check(!store.State.Projects.Contains(project) && store.State.Projects.Contains(retained), "別のプロジェクトを一覧から削除しました");
        check(File.Exists(Path.Combine(directory, "simulated-recycle/asset.txt")), "制作ファイルを完全削除しました");

        var failed = Create("Failure");
        Reject(() => service.Delete(failed, _ => throw new IOException("移動失敗")), "移動失敗を受理しました");
        check(store.State.Projects.Contains(failed) && File.Exists(failed.Descriptor), "移動失敗で登録か制作ファイルを失いました");
        Reject(() => service.Delete(failed, _ => { }), "移動していないのに成功しました");
        using (var cancel = new CancellationTokenSource()) {
            cancel.Cancel();
            Reject(() => service.Delete(failed, _ => check(false, "キャンセル後に移動しました"), cancel.Token), "キャンセルを無視しました");
        }
        check(store.State.Projects.Contains(failed), "キャンセル後に登録を解除しました");

        string session = Path.Combine(Path.GetDirectoryName(failed.Descriptor)!, "Library/EditorSession.lock");
        Directory.CreateDirectory(Path.GetDirectoryName(session)!);
        using (File.Open(session, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.Read))
            Reject(() => service.Delete(failed, _ => check(false, "起動中に移動しました")), "起動中のプロジェクトを削除しました");
        var nested = Create("Parent/Child");
        var parent = Create("Parent");
        Reject(() => service.GetDeletionDirectory(parent), "登録済みの子プロジェクトを巻き込みました");
        store.RemoveProject(nested.Descriptor);
        Reject(() => service.GetDeletionDirectory(parent), "未登録の子プロジェクトを巻き込みました");
        Reject(() => service.GetDeletionDirectory(failed with { Descriptor = Path.Combine(store.State.InstallRoot, "Test.nemproject") }), "エンジンの配置を削除対象にしました");
        Reject(() => service.GetDeletionDirectory(failed with { Descriptor = Path.Combine(Path.GetPathRoot(directory)!, "Test.nemproject") }), "ドライブ全体を削除対象にしました");
        Reject(() => service.GetDeletionDirectory(failed with { Descriptor = Path.Combine(Path.GetDirectoryName(failed.Descriptor)!, "Missing.nemproject") }), "情報がないフォルダーを削除対象にしました");

        var missing = Create("Missing");
        DistributionStorage.RemoveTree(Path.GetDirectoryName(missing.Descriptor)!, directory);
        service.Delete(missing, _ => check(false, "存在しないフォルダーを移動しました"));
        check(!store.State.Projects.Contains(missing) && store.State.Projects.Contains(failed), "存在しないプロジェクトを一覧から削除できません");
        CheckWindowsRecycle(directory, check);
    }

    // 専用データをごみ箱へ移し、Shellから元の場所へ復元する
    private static void CheckWindowsRecycle(string root, Action<bool, string> check) {
        string directory = Path.Combine(root, "Recycle-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(directory);
        File.WriteAllText(Path.Combine(directory, "asset.txt"), "recycle verification");
        string recycled = WindowsRecycleBin.Move(directory);
        check(!Directory.Exists(directory) && recycled.Contains("$Recycle.Bin", StringComparison.OrdinalIgnoreCase) &&
            File.ReadAllText(Path.Combine(recycled, "asset.txt")) == "recycle verification", "ごみ箱へ制作ファイルを保持できません");
        Exception? failure = null;
        var thread = new Thread(() => {
            try { RestoreFromRecycleBin(recycled); }
            catch (Exception error) { failure = error; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
        var watch = Stopwatch.StartNew();
        while (!Directory.Exists(directory) && watch.Elapsed < TimeSpan.FromSeconds(10)) Thread.Sleep(50);
        check(Directory.Exists(directory) && File.ReadAllText(Path.Combine(directory, "asset.txt")) == "recycle verification",
            "ごみ箱から元の制作ファイルを復元できません");
    }

    private static void RestoreFromRecycleBin(string recycled) {
        dynamic shell = Activator.CreateInstance(Type.GetTypeFromProgID("Shell.Application", true)!)!;
        dynamic bin = shell.NameSpace(10);
        dynamic items = bin.Items();
        try {
            for (int index = 0; index < (int)items.Count; ++index) {
                dynamic item = items.Item(index);
                try {
                    if (((string)item.Path).Equals(recycled, StringComparison.OrdinalIgnoreCase)) {
                        item.InvokeVerb("undelete");
                        return;
                    }
                } finally { Marshal.FinalReleaseComObject(item); }
            }
        } finally {
            Marshal.FinalReleaseComObject(items);
            Marshal.FinalReleaseComObject(bin);
            Marshal.FinalReleaseComObject(shell);
        }
    }
}
