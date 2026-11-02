using System.Runtime.Versioning;

namespace NEMEngine.Distribution;

public sealed partial class ProjectService {
    // 確認画面と実行直前で同じ削除範囲を検証する
    public string GetDeletionDirectory(ProjectEntry project, CancellationToken token = default) {
        string descriptor = Path.GetFullPath(project.Descriptor);
        string directory = Path.TrimEndingDirectorySeparator(Path.GetDirectoryName(descriptor)!);
        if (!Path.GetExtension(descriptor).Equals(".nemproject", StringComparison.OrdinalIgnoreCase) ||
            directory.Equals(Path.GetPathRoot(directory), StringComparison.OrdinalIgnoreCase))
            throw new IOException("プロジェクトの削除先が不正です");

        var protectedPaths = new List<string> { store.Root, store.State.InstallRoot, AppContext.BaseDirectory,
            Environment.GetFolderPath(Environment.SpecialFolder.Windows) };
        protectedPaths.AddRange(store.State.Engines.SelectMany(engine => new[] { engine.Root, engine.ToolchainRoot }));
        if (protectedPaths.Any(path => path.Length != 0 &&
                (DistributionStorage.IsInside(path, directory) || DistributionStorage.IsInside(directory, path))) ||
            DistributionStorage.IsInside(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), directory))
            throw new IOException("ランチャーやエンジンを含む場所は削除できません");
        if (store.State.Projects.Any(other => !other.Descriptor.Equals(descriptor, StringComparison.OrdinalIgnoreCase) &&
            DistributionStorage.IsInside(other.Descriptor, directory)))
            throw new IOException("別のプロジェクトを含むため削除できません");

        // リンク経由で別のフォルダーを削除しない
        for (var ancestor = new DirectoryInfo(directory); ancestor != null; ancestor = ancestor.Parent) {
            if (ancestor.Exists && (ancestor.Attributes & FileAttributes.ReparsePoint) != 0)
                throw new IOException("リンク経由のプロジェクトは削除できません");
        }
        if (!Directory.Exists(directory)) return directory;
        Read(descriptor);
        if (IsRunning(descriptor)) throw new IOException("保存してEditorを終了してからプロジェクトを削除してください");

        var pending = new Stack<string>();
        pending.Push(directory);
        while (pending.TryPop(out string? current)) {
            token.ThrowIfCancellationRequested();
            foreach (string entry in Directory.EnumerateFileSystemEntries(current)) {
                var attributes = File.GetAttributes(entry);
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("リンクを含むプロジェクトは削除できません: " + entry);
                if ((attributes & FileAttributes.Directory) != 0) pending.Push(entry);
                else if (Path.GetExtension(entry).Equals(".nemproject", StringComparison.OrdinalIgnoreCase) &&
                    !entry.Equals(descriptor, StringComparison.OrdinalIgnoreCase))
                    throw new IOException("別のプロジェクトを含むため削除できません: " + entry);
            }
        }
        return directory;
    }

    [SupportedOSPlatform("windows")]
    public void Delete(ProjectEntry project, CancellationToken token = default) =>
        Delete(project, directory => WindowsRecycleBin.Move(directory, token), token);

    internal void Delete(ProjectEntry project, Action<string> recycle, CancellationToken token = default) {
        string directory = GetDeletionDirectory(project, token);
        token.ThrowIfCancellationRequested();
        if (Directory.Exists(directory)) {
            recycle(directory);
            if (Directory.Exists(directory)) throw new IOException("プロジェクトをごみ箱へ移動できませんでした");
        }
        // 移動が完了するまで一覧の登録を残す
        store.RemoveProject(project.Descriptor);
    }
}
