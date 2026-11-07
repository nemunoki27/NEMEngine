namespace NEMEngine.Distribution;

// 必要なときだけ取得するサンプルプロジェクト
public sealed record SampleRelease {
    public string ID { get; init; } = "";
    public string Name { get; init; } = "";
    public string Description { get; init; } = "";
    public string Version { get; init; } = "";
    public string EngineVersion { get; init; } = "";
    public PackageInfo Package { get; init; } = new();
}

// ZIPで配布するアセットの表示情報
public sealed record AssetRelease {
    public string ID { get; init; } = "";
    public string Name { get; init; } = "";
    public string Description { get; init; } = "";
    public string Version { get; init; } = "";
    public string RecommendedEngineVersion { get; init; } = "";
    public string FileName { get; init; } = "";
    public PackageInfo Package { get; init; } = new();
}

// テンプレート保存元とダウンロードしたサンプルの共通情報
public sealed record ProjectTemplateManifest {
    public int SchemaVersion { get; init; } = 1;
    public string ID { get; init; } = "";
    public string Name { get; init; } = "";
    public string Description { get; init; } = "";
    public string Kind { get; init; } = "";
    public string StartupScene { get; init; } = "";
    public List<string> AdditionalPaths { get; init; } = [];
}
