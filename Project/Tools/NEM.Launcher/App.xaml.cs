using System.IO;
using System.IO.Pipes;
using System.Windows;

namespace NEMEngine.Launcher;

public partial class App : Application {
    private readonly bool launchWindow_;
    private Mutex? instance_;
    private readonly CancellationTokenSource shutdown_ = new();
    private readonly string pipeName_ = "NEMLauncher-" + Environment.UserName;

    public App() : this(true) { }
    internal App(bool launchWindow) { launchWindow_ = launchWindow; }

    protected override async void OnStartup(StartupEventArgs e) {
        if (!launchWindow_) return;
        base.OnStartup(e);
        DispatcherUnhandledException += (_, failure) => {
            if (MainWindow is MainWindow window) window.ShowError(failure.Exception);
            else MessageBox.Show(failure.Exception.Message, "NEMLauncher");
            failure.Handled = true;
        };
        string project = e.Args.Length == 2 && e.Args[0] == "--project" ? e.Args[1] : "";
        bool connect = e.Args.Length == 1 && e.Args[0] == "--connect-github";
        instance_ = new Mutex(true, "Local\\" + pipeName_, out bool created);
        if (!created) {
            try {
                await using var client = new NamedPipeClientStream(".", pipeName_, PipeDirection.Out, PipeOptions.Asynchronous);
                await client.ConnectAsync(3000);
                await using var writer = new StreamWriter(client);
                await writer.WriteLineAsync(connect ? "@connect-github" : project);
                await writer.FlushAsync();
            } catch (Exception error) { MessageBox.Show(error.Message, "NEMLauncher"); }
            Shutdown();
            return;
        }
        try {
            var window = new MainWindow();
            MainWindow = window;
            window.Show();
            _ = ReceiveProjectsAsync(window);
            if (connect) await window.ConnectGitHubFromCommandAsync();
            else if (project.Length != 0) await window.OpenDescriptorAsync(project);
        } catch (Exception error) {
            MessageBox.Show(error.Message, "NEMLauncher", MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(1);
        }
    }

    private async Task ReceiveProjectsAsync(MainWindow window) {
        try {
            while (!shutdown_.IsCancellationRequested) {
                await using var server = new NamedPipeServerStream(pipeName_, PipeDirection.In, 1,
                    PipeTransmissionMode.Byte, PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                await server.WaitForConnectionAsync(shutdown_.Token);
                using var reader = new StreamReader(server);
                string project = await reader.ReadLineAsync(shutdown_.Token) ?? "";
                await (await Dispatcher.InvokeAsync(async () => {
                    window.WindowState = WindowState.Normal;
                    window.Activate();
                    if (project == "@connect-github") await window.ConnectGitHubFromCommandAsync();
                    else if (project.Length != 0) await window.OpenDescriptorAsync(project);
                }));
            }
        } catch (OperationCanceledException) { }
        catch (Exception error) { await Dispatcher.InvokeAsync(() => window.ShowError(error)); }
    }

    protected override void OnExit(ExitEventArgs e) {
        shutdown_.Cancel();
        instance_?.Dispose();
        shutdown_.Dispose();
        base.OnExit(e);
    }
}
