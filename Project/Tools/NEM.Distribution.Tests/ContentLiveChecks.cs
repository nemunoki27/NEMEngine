using NEMEngine.Distribution;

// 明示された配布一覧から実際のZIPを取得する
internal static class ContentLiveChecks {
    public static async Task RunAsync(string repository, string root, string[] args, Action<bool, string> check) {
        string? Option(string name) => args.FirstOrDefault(value => value.StartsWith(name + "=", StringComparison.Ordinal))?[(name.Length + 1)..];
        string source = Option("--content-live")!;
        using var client = new HttpClient { Timeout = Timeout.InfiniteTimeSpan };
        var catalog = Uri.TryCreate(source, UriKind.Absolute, out var uri) && uri.Scheme == "https"
            ? await CatalogService.FetchAsync(client, source, default)
            : DistributionStorage.Read<ReleaseCatalog>(Path.GetFullPath(source));
        if (catalog.SchemaVersion != 1) throw new InvalidDataException("対応する配布一覧ではありません");
        ContentValidation.Validate(catalog);
        string? version = Option("--content-version");
        string? sampleID = Option("--content-sample");
        var sample = catalog.Samples.Where(item => (version == null || item.EngineVersion == version) && (sampleID == null || item.ID == sampleID))
            .OrderByDescending(item => Version.Parse(item.EngineVersion)).ThenByDescending(item => Version.Parse(item.Version)).FirstOrDefault()
            ?? throw new InvalidDataException("検証するサンプルが配布一覧にありません");
        var registered = new LauncherStore().State.Engines.FirstOrDefault(engine => engine.Version == sample.EngineVersion);
        string engineRoot = Option("--content-engine-root") ?? registered?.Root ?? throw new InvalidDataException("検証用のエンジンをインストールするか--content-engine-rootを指定してください");
        string toolchainRoot = Option("--content-toolchain-root") ?? registered?.ToolchainRoot ?? throw new InvalidDataException("--content-toolchain-rootを指定してください");
        var store = new LauncherStore(Path.Combine(root, "ContentLive/State"));
        store.Register(new InstalledEngine { Version = sample.EngineVersion, Root = engineRoot, ToolchainRoot = toolchainRoot });
        var service = new ProjectService(store);
        var project = await service.CreateSampleAsync("配布サンプル確認", Path.Combine(root, "ContentLive/Projects"), sample.EngineVersion, sample, client,
            new ConsoleProgress(), default);
        string projectRoot = Path.GetDirectoryName(project.Descriptor)!;
        check(ProjectService.Read(project.Descriptor).EngineVersion == sample.EngineVersion &&
            File.Exists(Path.Combine(projectRoot, "Scripts/GameScripts.csproj")) &&
            File.Exists(Path.Combine(projectRoot, "ProjectSettings/Runtime/StartupScene.json")), "公開サンプルからプロジェクトを作成できません");
        string? assetID = Option("--content-asset");
        var asset = catalog.Assets.Where(item => assetID == null || item.ID == assetID)
            .OrderByDescending(item => Version.Parse(item.Version)).FirstOrDefault()
            ?? throw new InvalidDataException("検証するアセットが配布一覧にありません");
        string saved = Path.Combine(root, "ContentLive", asset.FileName);
        await new AssetDownloadService(client).SaveAsync(asset, Path.Combine(store.Root, "Downloads"), saved, false,
            new ConsoleProgress(), default);
        check(await DistributionStorage.HashAsync(saved) == asset.Package.Sha256, "公開アセットの内容が一致しません");
        DistributionStorage.Write(Path.Combine(repository, "Generated/ContentDistributionValidation/live.json"), new {
            success = true, engineVersion = sample.EngineVersion, sample = new { sample.ID, sample.Version, sample.Package.Bytes },
            asset = new { asset.ID, asset.Version, asset.FileName, asset.Package.Bytes }
        });
    }

    private sealed class ConsoleProgress : IProgress<TransferProgress> {
        private string message_ = "";
        public void Report(TransferProgress value) {
            if (message_ == value.Message) return;
            message_ = value.Message;
            Console.WriteLine(value.Message);
        }
    }
}
