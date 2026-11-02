using System.Text;
using System.Text.Json.Nodes;

namespace NEMEngine.Distribution;

public sealed partial class GitProjectService {
    private sealed record CloneState(string URL, string Destination, string Stage, bool Cloned);

    public async Task<ProjectEntry> CloneAsync(string url, string parent, string name, IProgress<TransferProgress>? progress, CancellationToken token) {
        ProjectService.ValidateName(name);
        string fullName = GitHubRepositories.ParseURL(url);
        var identity = RequireIdentity();
        var repository = await repositories.ForAccount(identity.ID).GetAsync(fullName, token);
        string destination = DistributionStorage.Child(parent, name);
        if (DistributionStorage.IsInside(destination, store.Root) || DistributionStorage.IsInside(destination, store.State.InstallRoot) ||
            store.State.Engines.Any(engine => DistributionStorage.IsInside(destination, engine.Root) || DistributionStorage.IsInside(destination, engine.ToolchainRoot)))
            throw new IOException("ランチャーやエンジンの管理フォルダーには取得できません");
        Directory.CreateDirectory(parent);
        await using var operation = await OperationFileLock.AcquireAsync(store.Root, "project:" + destination.ToUpperInvariant(), token);
        string operationID = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(Encoding.UTF8.GetBytes(destination.ToUpperInvariant())));
        string stateFile = Path.Combine(store.Root, "Clones", operationID + ".json");
        var state = File.Exists(stateFile) ? DistributionStorage.Read<CloneState>(stateFile) : new(repository.CloneURL, destination,
            DistributionStorage.Child(parent, ".nem-clone-" + Guid.NewGuid().ToString("N")), false);
        if (state.URL != repository.CloneURL || state.Destination != destination ||
            !Path.GetDirectoryName(state.Stage)!.Equals(Path.TrimEndingDirectorySeparator(Path.GetFullPath(parent)), StringComparison.OrdinalIgnoreCase) ||
            !Path.GetFileName(state.Stage).StartsWith(".nem-clone-", StringComparison.Ordinal))
            throw new IOException("別の取得処理が同じ保存先を使用しています 保存先を変更してください");
        // 公開後の登録だけ失敗した場合も、完成済みの取得先を利用する
        if (Directory.Exists(destination)) {
            var saved = GitProjectBinding.Read(destination);
            if (!File.Exists(stateFile) || saved?.Repository?.ID != repository.ID || !saved.UploadComplete)
                throw new IOException("同じ名前の保存先が既に存在します");
            var recovered = FindClonedProject(destination);
            store.Register(recovered);
            File.Delete(stateFile);
            return recovered;
        }
        var runner = await RunnerAsync(progress, token);
        DistributionStorage.Write(stateFile, state);
        if (!state.Cloned) {
            if (Directory.Exists(state.Stage)) DistributionStorage.RemoveTree(state.Stage, parent);
            progress?.Report(new("プロジェクトを取得中"));
            await runner.RunAsync(parent, ["clone", "--", repository.CloneURL, state.Stage], token, fullName: repository.FullName, accountID: identity.ID);
            state = state with { Cloned = true };
            DistributionStorage.Write(stateFile, state);
        }
        var project = FindClonedProject(state.Stage);
        var worktree = new GitWorktree(runner);
        await worktree.ConfigureLFSAsync(state.Stage, token);
        var previousBinding = GitProjectBinding.Read(state.Stage);
        var binding = (previousBinding?.Repository?.ID == repository.ID ? previousBinding : new GitProjectBinding()) with {
            Repository = repository, AccountID = identity.ID, UploadComplete = false
        };
        binding = await worktree.ConfigureCredentialsAsync(state.Stage, binding, token);
        binding.Save(state.Stage);
        progress?.Report(new("LFSのAssetを取得中"));
        await runner.RunAsync(state.Stage, ["lfs", "pull"], token, fullName: repository.FullName, accountID: identity.ID);
        await runner.RunAsync(state.Stage, ["lfs", "fsck"], token);
        var lfs = await runner.RunAsync(state.Stage, ["lfs", "ls-files", "--long"], token);
        if (System.Text.RegularExpressions.Regex.IsMatch(lfs.Output, @"(?m)^[a-f0-9]{64} - "))
            throw new IOException("LFSの取得が完了していません 同じ保存先で取得を再実行してください");
        (binding with { UploadComplete = true }).Save(state.Stage);
        token.ThrowIfCancellationRequested();
        Directory.Move(state.Stage, destination);
        project = ProjectService.Read(Path.Combine(destination, Path.GetFileName(project.Descriptor)));
        store.Register(project);
        var engine = store.State.Engines.FirstOrDefault(item => item.Version == project.EngineVersion);
        if (engine != null) ProjectService.Bind(project.Descriptor, engine);
        File.Delete(stateFile);
        return project;
    }

    private static ProjectEntry FindClonedProject(string root) {
        if ((File.GetAttributes(root) & FileAttributes.ReparsePoint) != 0) throw new IOException("リンクの取得先は利用できません");
        string[] descriptors = Directory.GetFiles(root, "*.nemproject");
        if (descriptors.Length != 1) throw new IOException("取得したリポジトリの直下に.nemprojectが一つ必要です");
        var data = JsonNode.Parse(File.ReadAllText(descriptors[0]))!.AsObject();
        foreach (string key in new[] { "assetsDirectory", "packagesDirectory", "projectSettingsDirectory" })
            DistributionStorage.Child(root, data[key]?.GetValue<string>() ?? throw new InvalidDataException("プロジェクトの保存先がありません"));
        return ProjectService.Read(descriptors[0]);
    }
}
