namespace NEMEngine.Distribution;

// 利用者ごとの一覧と設定を所有する
public sealed class LauncherStore {
    private readonly string statePath_;
    public string Root { get; }
    public LauncherState State { get; private set; }

    public LauncherStore(string? root = null) {
        Root = root ?? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "NEMEngine", "Launcher");
        statePath_ = Path.Combine(Root, "state.json");
        State = File.Exists(statePath_) ? DistributionStorage.Read<LauncherState>(statePath_) : new();
    }

    private void Update(Action<LauncherState> action) {
        Directory.CreateDirectory(Root);
        using var operation = new FileStream(Path.Combine(Root, "state.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
        var next = File.Exists(statePath_) ? DistributionStorage.Read<LauncherState>(statePath_) : new();
        action(next);
        DistributionStorage.Write(statePath_, next);
        State = next;
    }

    public void SaveSettings() {
        string installRoot = State.InstallRoot, projectsRoot = State.ProjectsRoot, catalogUrl = State.CatalogUrl;
        Update(next => { next.InstallRoot = installRoot; next.ProjectsRoot = projectsRoot; next.CatalogUrl = catalogUrl; });
    }

    public void Register(InstalledEngine engine) {
        Update(next => {
            var previous = next.Engines.FirstOrDefault(item => item.Version == engine.Version);
            if (previous != null && !Path.GetFullPath(previous.Root).Equals(Path.GetFullPath(engine.Root), StringComparison.OrdinalIgnoreCase))
                throw new IOException("同じ版が別のフォルダーに登録されています 先に登録を解除してください");
            next.Engines.RemoveAll(item => item.Version == engine.Version);
            next.Engines.Add(previous is { Owned: true } && !engine.Owned ? engine with { Owned = true, OwnerRoot = previous.OwnerRoot } : engine);
        });
    }

    public void Register(ProjectEntry project) {
        Update(next => {
            next.Projects.RemoveAll(item => item.Descriptor.Equals(project.Descriptor, StringComparison.OrdinalIgnoreCase));
            next.Projects.Add(project);
        });
    }

    public void RemoveProject(string descriptor) => Update(next => next.Projects.RemoveAll(item => item.Descriptor.Equals(descriptor, StringComparison.OrdinalIgnoreCase)));
    public void RemoveEngine(string version) => Update(next => next.Engines.RemoveAll(item => item.Version == version));

    public void ReplaceProject(string previousDescriptor, ProjectEntry project) => Update(next => {
        next.Projects.RemoveAll(item => item.Descriptor.Equals(previousDescriptor, StringComparison.OrdinalIgnoreCase) ||
            item.Descriptor.Equals(project.Descriptor, StringComparison.OrdinalIgnoreCase));
        next.Projects.Add(project);
    });

    // 他メンバーの改名も同じフォルダー内のGUIDで追跡する
    public void RefreshProjects() {
        var changes = new List<(ProjectEntry Previous, ProjectEntry Current)>();
        foreach (var previous in State.Projects) {
            try {
                ProjectEntry? current = null;
                if (File.Exists(previous.Descriptor)) current = ProjectService.Read(previous.Descriptor);
                else if (previous.ProjectGUID.Length != 0 && Directory.Exists(Path.GetDirectoryName(previous.Descriptor))) {
                    var candidates = Directory.EnumerateFiles(Path.GetDirectoryName(previous.Descriptor)!, "*.nemproject")
                        .Select(ProjectService.Read).Where(item => item.ProjectGUID == previous.ProjectGUID).ToArray();
                    if (candidates.Length == 1) current = candidates[0];
                }
                if (current != null) {
                    current = current with { LastOpened = previous.LastOpened };
                    if (current != previous) changes.Add((previous, current));
                }
            } catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or System.Text.Json.JsonException or ArgumentException or InvalidOperationException) { }
        }
        if (changes.Count == 0) return;
        Update(next => {
            foreach (var change in changes) {
                int index = next.Projects.FindIndex(item => item.Descriptor.Equals(change.Previous.Descriptor, StringComparison.OrdinalIgnoreCase));
                if (index >= 0) next.Projects[index] = change.Current;
            }
        });
    }

    public InstalledEngine Resolve(string version) {
        return State.Engines.SingleOrDefault(engine => engine.Version == version)
            ?? throw new InvalidOperationException("使用版がインストールされていません: " + version);
    }
}
