using System.Runtime.ExceptionServices;
using System.Windows;
using NEMEngine.Distribution;
using NEMEngine.Launcher;

// 利用者の設定を変更せず実際のランチャーを操作確認する
internal static class ContentWindowSession {
    public static void Run(string repository, string[] args) {
        Exception? failure = null;
        var thread = new Thread(() => {
            try { Show(repository, args); }
            catch (Exception error) { failure = error; }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        thread.Join();
        if (failure != null) ExceptionDispatchInfo.Capture(failure).Throw();
    }

    private static void Show(string repository, string[] args) {
        string? Option(string name) => args.FirstOrDefault(value => value.StartsWith(name + "=", StringComparison.Ordinal))?[(name.Length + 1)..];
        string engineRoot = Path.GetFullPath(Option("--content-engine-root") ?? throw new ArgumentException("--content-engine-rootを指定してください"));
        string toolchainRoot = Path.GetFullPath(Option("--content-toolchain-root") ?? throw new ArgumentException("--content-toolchain-rootを指定してください"));
        var manifest = EngineValidation.Read(engineRoot);
        if (!File.Exists(Path.Combine(toolchainRoot, "dotnet.exe"))) throw new IOException("C#ツールチェーンが見つかりません");
        string directory = Path.Combine(repository, "Generated/ContentDistributionValidation");
        var store = new LauncherStore(Path.Combine(directory, "UIState"));
        store.State.InstallRoot = Path.Combine(store.Root, "Installations");
        store.State.ProjectsRoot = Path.Combine(directory, "UIProjects");
        string config = Option("--content-config") ?? Path.Combine(repository, "Generated/Distribution/Launcher-" + DistributionVersion.Current + "/App/Config");
        string? catalogURL = Option("--content-catalog-url");
        if (catalogURL == null && File.Exists(Path.Combine(config, "launcher.json")))
            catalogURL = DistributionStorage.Read<LauncherConfiguration>(Path.Combine(config, "launcher.json")).CatalogUrl;
        if (!Uri.TryCreate(catalogURL, UriKind.Absolute, out var uri) || uri.Scheme != "https")
            throw new ArgumentException("--content-catalog-urlまたは--content-configで配布一覧を指定してください");
        store.State.CatalogUrl = uri.AbsoluteUri;
        store.SaveSettings();
        store.Register(new InstalledEngine { Version = manifest.Version, Root = engineRoot, ToolchainRoot = toolchainRoot });
        DistributionStorage.Write(Path.Combine(store.Root, "session.json"), new {
            processID = Environment.ProcessId, engineRoot, toolchainRoot, catalogUrl = uri.AbsoluteUri, startedAt = DateTimeOffset.Now
        });
        Console.WriteLine("操作確認用ランチャー: PID=" + Environment.ProcessId + " / " + store.Root);
        var app = new App(false) { ShutdownMode = ShutdownMode.OnMainWindowClose };
        app.InitializeComponent();
        var window = new MainWindow(store, config);
        app.DispatcherUnhandledException += (_, error) => { window.ShowError(error.Exception); error.Handled = true; };
        app.Run(window);
    }
}
