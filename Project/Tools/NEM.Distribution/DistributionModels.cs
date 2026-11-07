using System.Text.Json.Serialization;

namespace NEMEngine.Distribution;

// 配信するアーカイブの検証情報
public sealed record PackageInfo {
    public string ID { get; init; } = "";
    public string Url { get; init; } = "";
    public string Sha256 { get; init; } = "";
    public long Bytes { get; init; }
    public long UnpackedBytes { get; init; }
}

// エンジンと共有ツールチェーンの組合せ
public sealed record EngineRelease {
    public string Version { get; init; } = "";
    public string Notes { get; init; } = "";
    public string ToolchainVersion { get; init; } = "";
    public string MinimumLauncherVersion { get; init; } = "0.1.0";
    public PackageInfo Engine { get; init; } = new();
    public PackageInfo Toolchain { get; init; } = new();
}

public sealed record LauncherRelease {
    public string Version { get; init; } = "";
    public PackageInfo Setup { get; init; } = new();
}

public sealed record ReleaseCatalog {
    public int SchemaVersion { get; init; } = 1;
    public LauncherRelease? Launcher { get; init; }
    public List<EngineRelease> Engines { get; init; } = [];
    public List<SampleRelease> Samples { get; init; } = [];
    public List<AssetRelease> Assets { get; init; } = [];
}

public sealed record EngineFile {
    public string Path { get; init; } = "";
    public string Sha256 { get; init; } = "";
    public long Bytes { get; init; }
}

// 配布フォルダーに含まれる実行入口
public sealed record EngineManifest {
    public const string FileName = "engine.json";
    public int SchemaVersion { get; init; } = 1;
    public string Version { get; init; } = "";
    public string Platform { get; init; } = "win-x64";
    public string BuildID { get; init; } = "";
    public string ToolchainVersion { get; init; } = "";
    public string Editor { get; init; } = "Editor/Develop/NEMEditor.exe";
    public string ProjectTool { get; init; } = "Tools/ProjectTool/NEM.ProjectTool.dll";
    public List<EngineFile> Files { get; init; } = [];
}

public sealed record InstalledEngine {
    public string Version { get; init; } = "";
    public string Root { get; init; } = "";
    public string ToolchainRoot { get; init; } = "";
    public bool Owned { get; init; }
    public string OwnerRoot { get; init; } = "";
    [JsonIgnore] public string Status => Directory.Exists(Root) ? "インストール済み" : "見つかりません";
}

public sealed record ProjectEntry {
    public string ProjectGUID { get; init; } = "";
    public string Name { get; init; } = "";
    public string Descriptor { get; init; } = "";
    public string EngineVersion { get; init; } = "";
    public DateTime LastOpened { get; init; }
    [JsonIgnore] public string Status => File.Exists(Descriptor) ? "" : "見つかりません";
}

public sealed class LauncherState {
    public string InstallRoot { get; set; } = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NEMEngine", "Installations");
    public string ProjectsRoot { get; set; } = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments), "NEMProjects");
    public string CatalogUrl { get; set; } = "";
    public List<InstalledEngine> Engines { get; set; } = [];
    public List<ProjectEntry> Projects { get; set; } = [];
}

public sealed record TransferProgress(string Message, long Completed = 0, long Total = 0) {
    public double Percentage => Total > 0 ? Math.Clamp(100.0 * Completed / Total, 0, 100) : 0;
}
