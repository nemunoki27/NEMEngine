using System.Text.Json.Nodes;

namespace NEMEngine.Distribution;

public sealed partial class ProjectService {
    public async Task<ProjectEntry> CreateSampleAsync(string name, string parent, string version, SampleRelease sample,
        HttpClient client, IProgress<TransferProgress>? progress, CancellationToken token) {
        ContentValidation.Validate(sample);
        if (sample.EngineVersion != version) throw new InvalidDataException("サンプルが指定されたエンジンの版に対応していません");
        ValidateCreation(name, parent, version);
        string cache = Path.Combine(store.Root, "Downloads");
        string archive = await new PackageTransfer(client).DownloadAsync(sample.Package, cache, progress, token);
        string stage = DistributionStorage.Child(cache, ".sample-" + Guid.NewGuid().ToString("N"));
        try {
            await PackageTransfer.ExtractAsync(archive, stage, sample.Package.UnpackedBytes, progress, token);
            progress?.Report(new("サンプルからプロジェクトを作成中"));
            return CreateFromTemplate(name, parent, version, stage, "Sample", sample.ID, token);
        } finally { DistributionStorage.RemoveTree(stage, cache); }
    }

    // 使用版と保存先をダウンロードの前にも確認する
    private InstalledEngine ValidateCreation(string name, string parent, string version) {
        ValidateName(name);
        var installed = store.Resolve(version);
        if (EngineValidation.Read(installed.Root).Version != version) throw new InvalidDataException("エンジンの版が一致しません");
        string destination = DistributionStorage.Child(parent, name);
        if (Directory.Exists(destination) || File.Exists(destination)) throw new IOException("同じ名前の保存先が既に存在します");
        if (DistributionStorage.IsInside(destination, store.State.InstallRoot) ||
            store.State.Engines.Any(engine => DistributionStorage.IsInside(destination, engine.Root) || DistributionStorage.IsInside(destination, engine.ToolchainRoot)))
            throw new IOException("エンジンのインストール先にはプロジェクトを作成できません");
        return installed;
    }

    // 標準テンプレートとサンプルを同じ手順で作成する
    private ProjectEntry CreateFromTemplate(string name, string parent, string version, string source,
        string kind, string? sampleID, CancellationToken token) {
        var installed = ValidateCreation(name, parent, version);
        string metadata = Path.Combine(source, "template.json");
        ProjectTemplateManifest? template = File.Exists(metadata) ? DistributionStorage.Read<ProjectTemplateManifest>(metadata) : null;
        if (template == null && sampleID != null) throw new InvalidDataException("サンプルのテンプレート情報がありません");
        if (template != null && (template.SchemaVersion != 1 || template.Kind != kind ||
            (sampleID != null && template.ID != sampleID) || !Guid.TryParseExact(template.StartupScene, "N", out _)))
            throw new InvalidDataException("テンプレートの保存情報が配布一覧と一致しません");
        string destination = DistributionStorage.Child(parent, name);
        Directory.CreateDirectory(parent);
        string stage = DistributionStorage.Child(parent, ".project-" + Guid.NewGuid().ToString("N"));
        bool published = false;
        try {
            CopyTemplate(Path.Combine(installed.Root, "ProjectTemplates", "Common"), stage, token);
            CopyTemplate(source, stage, token);
            if (template != null) {
                ValidateStartupScene(stage, template.StartupScene);
                DistributionStorage.Write(Path.Combine(stage, "ProjectSettings/Runtime/StartupScene.json"), new { activeScene = template.StartupScene });
            }
            string descriptorName = name + ".nemproject";
            DistributionStorage.Write(Path.Combine(stage, descriptorName), new {
                schemaVersion = 1, projectGuid = Guid.NewGuid().ToString("N"), name, engineVersion = version,
                sceneStorage = "Monolithic", assetsDirectory = "GameAssets", packagesDirectory = "Packages",
                projectSettingsDirectory = "ProjectSettings"
            });
            Bind(Path.Combine(stage, descriptorName), installed);
            token.ThrowIfCancellationRequested();
            Directory.Move(stage, destination);
            published = true;
            var project = Read(Path.Combine(destination, descriptorName));
            store.Register(project);
            published = false;
            return project;
        } catch {
            if (published) Directory.Move(destination, stage);
            throw;
        } finally { DistributionStorage.RemoveTree(stage, parent); }
    }

    // 説明情報と生成物をプロジェクトへ混ぜない
    private static void CopyTemplate(string source, string destination, CancellationToken token) {
        foreach (string entry in Directory.EnumerateFileSystemEntries(source)) {
            token.ThrowIfCancellationRequested();
            string name = Path.GetFileName(entry);
            if (name == "template.json") continue;
            if (name is not ("GameAssets" or "Scripts" or "ProjectSettings" or "Packages" or ".gitignore" or ".gitattributes"))
                throw new InvalidDataException("テンプレートに配布対象外の項目があります: " + name);
            CopyTemplateEntry(entry, DistributionStorage.Child(destination, name), token);
        }
    }

    // スナップショットの設定を共通雛形へ重ねる
    private static void CopyTemplateEntry(string source, string destination, CancellationToken token) {
        token.ThrowIfCancellationRequested();
        if ((File.GetAttributes(source) & FileAttributes.ReparsePoint) != 0) throw new IOException("リンクを含むテンプレートは使用できません");
        if (Directory.Exists(source)) {
            Directory.CreateDirectory(destination);
            foreach (string entry in Directory.EnumerateFileSystemEntries(source))
                CopyTemplateEntry(entry, DistributionStorage.Child(destination, Path.GetFileName(entry)), token);
        } else {
            Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
            File.Copy(source, destination, true);
            File.SetAttributes(destination, File.GetAttributes(destination) & ~FileAttributes.ReadOnly);
        }
    }

    // 保存した開始シーンのGUIDが含まれていることを確認する
    private static void ValidateStartupScene(string root, string startupScene) {
        foreach (string content in new[] { "GameAssets", "Packages" }) {
            string directory = Path.Combine(root, content);
            if (!Directory.Exists(directory)) continue;
            foreach (string path in Directory.EnumerateFiles(directory, "*.scene.json.meta", SearchOption.AllDirectories)) {
                var metadata = JsonNode.Parse(File.ReadAllText(path));
                if (string.Equals(metadata?["guid"]?.GetValue<string>(), startupScene, StringComparison.OrdinalIgnoreCase) && File.Exists(path[..^5])) return;
            }
        }
        throw new InvalidDataException("テンプレートの開始シーンが見つかりません");
    }
}
