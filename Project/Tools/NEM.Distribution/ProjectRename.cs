using System.Text.Json.Nodes;

namespace NEMEngine.Distribution;

public sealed partial class ProjectService {
    public static void ValidateName(string name) {
        if (string.IsNullOrWhiteSpace(name) || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 ||
            name.EndsWith('.') || name.EndsWith(' ') || name is "." or "..")
            throw new ArgumentException("プロジェクト名に使用できない文字が含まれています");
        string stem = name.Split('.')[0].ToUpperInvariant();
        if (stem is "CON" or "PRN" or "AUX" or "NUL" ||
            (stem.Length == 4 && (stem.StartsWith("COM") || stem.StartsWith("LPT")) && stem[3] is >= '1' and <= '9'))
            throw new ArgumentException("Windowsの予約名は使用できません");
    }

    public ProjectEntry Rename(ProjectEntry project, string name, CancellationToken token = default) {
        ValidateName(name);
        string root = GetDeletionDirectory(project, token);
        var actual = Read(project.Descriptor);
        string parent = Path.GetDirectoryName(root)!;
        string destination = DistributionStorage.Child(parent, name);
        string oldFile = Path.GetFileName(project.Descriptor);
        string newFile = name + ".nemproject";
        string oldSolution = Path.GetFileNameWithoutExtension(oldFile) + ".slnx";
        string newSolution = name + ".slnx";
        if (name == actual.Name && root == destination && oldFile == newFile) return actual with { LastOpened = project.LastOpened };
        if (!root.Equals(destination, StringComparison.OrdinalIgnoreCase) && (Directory.Exists(destination) || File.Exists(destination)))
            throw new IOException("同じ名前の保存先が既に存在します");
        foreach (string file in new[] { newFile, newSolution }) {
            if (!file.Equals(file == newFile ? oldFile : oldSolution, StringComparison.OrdinalIgnoreCase) && File.Exists(Path.Combine(root, file)))
                throw new IOException("同じ名前のファイルが既に存在します: " + file);
        }
        if (OperatingSystem.IsWindows() && VisualStudioInstances.IsOpen(Path.Combine(root, oldSolution)))
            throw new IOException("Visual StudioのSolutionを閉じてから名前を変更してください");
        using (File.Open(project.Descriptor, FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { }
        if (File.Exists(Path.Combine(root, oldSolution)))
            using (File.Open(Path.Combine(root, oldSolution), FileMode.Open, FileAccess.ReadWrite, FileShare.None)) { }
        string original = File.ReadAllText(project.Descriptor);
        var data = JsonNode.Parse(original)!.AsObject();
        data["name"] = name;
        string stage = DistributionStorage.Child(parent, ".project-rename-" + Guid.NewGuid().ToString("N"));
        string current = root;
        bool descriptorMoved = false, solutionMoved = false;
        token.ThrowIfCancellationRequested();
        try {
            // 大小文字だけの変更も一度別名を経由する
            Directory.Move(root, stage);
            current = stage;
            if (oldFile != newFile) { MoveNamedFile(stage, oldFile, newFile); descriptorMoved = true; }
            if (oldSolution != newSolution && File.Exists(Path.Combine(stage, oldSolution))) {
                MoveNamedFile(stage, oldSolution, newSolution); solutionMoved = true;
            }
            DistributionStorage.Write(Path.Combine(stage, newFile), data);
            Directory.Move(stage, destination);
            current = destination;
            string descriptor = Path.Combine(destination, newFile);
            // エンジン参照とSolution内の相対パスは移動前の設定を維持する
            var renamed = Read(descriptor) with { LastOpened = project.LastOpened };
            store.ReplaceProject(project.Descriptor, renamed);
            return renamed;
        } catch (Exception failure) {
            if (current == root) throw;
            try {
                // 保存失敗でも制作ファイルと旧登録を残す
                if (descriptorMoved) MoveNamedFile(current, newFile, oldFile);
                File.WriteAllText(Path.Combine(current, oldFile), original);
                if (solutionMoved) MoveNamedFile(current, newSolution, oldSolution);
                if (current != root) Directory.Move(current, root);
            } catch (Exception recovery) { throw new AggregateException("名前を復元できません 保存先を確認してください: " + current, failure, recovery); }
            throw;
        }
    }

    private static void MoveNamedFile(string root, string previous, string next) {
        string stage = Path.Combine(root, ".rename-" + Guid.NewGuid().ToString("N"));
        File.Move(Path.Combine(root, previous), stage);
        try { File.Move(stage, Path.Combine(root, next)); }
        catch { File.Move(stage, Path.Combine(root, previous)); throw; }
    }
}
