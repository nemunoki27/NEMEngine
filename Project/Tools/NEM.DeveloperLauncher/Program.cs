using System.IO;
using System.IO.Pipes;
using System.Security.Cryptography;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using System.Text;
using System.Windows;

namespace NEMEngine.DeveloperLauncher;

internal static class Program {
    [STAThread]
    private static int Main(string[] args) {
        try {
            string? root = null, editor = null;
            for (int i = 0; i < args.Length; i += 2) {
                if (i + 1 == args.Length)
                    throw new ArgumentException("起動引数の値がありません");
                switch (args[i]) {
                    case "--repository":
                        root = args[i + 1];
                        break;
                    case "--editor-pipe":
                        editor = args[i + 1];
                        break;
                    default:
                        throw new ArgumentException("起動引数が不正です");
                }
            }
            root = root == null ? FindRepository() : Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
            if (!File.Exists(Path.Combine(root, "Tools/Distribution/PackageEngine.ps1")))
                throw new DirectoryNotFoundException("エンジン開発リポジトリが見つかりません");
            var key = "NEMDeveloperLauncher-" + Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(root.ToUpperInvariant())))[..24];
            using var singleton = new Mutex(true, key, out bool first);
            if (!first) {
                using var pipe = new NamedPipeClientStream(".", key, PipeDirection.Out, PipeOptions.CurrentUserOnly);
                pipe.Connect(5000);
                if (GetNamedPipeServerProcessId(pipe.SafePipeHandle, out uint processId))
                    AllowSetForegroundWindow(processId);
                using var writer = new StreamWriter(pipe, new UTF8Encoding(false));
                writer.WriteLine(editor ?? "");
                return 0;
            }
            var app = new Application { ShutdownMode = ShutdownMode.OnMainWindowClose };
            app.Resources.MergedDictionaries.Add(new ResourceDictionary { Source = new Uri("/NEMDeveloperLauncher;component/LauncherTheme.xaml", UriKind.Relative) });
            var window = new MainWindow(root, editor);
            using var shutdown = new CancellationTokenSource();
            var accept = AcceptActivations(key, window, shutdown.Token);
            try { return app.Run(window); } finally {
                shutdown.Cancel();
                singleton.ReleaseMutex();
            }
        } catch (Exception ex) {
            MessageBox.Show(ex.Message, "開発者ランチャー", MessageBoxButton.OK, MessageBoxImage.Error);
            return 1;
        }
    }

    [DllImport("kernel32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetNamedPipeServerProcessId(SafePipeHandle pipe, out uint processId);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AllowSetForegroundWindow(uint processId);

    private static string FindRepository() {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory != null; directory = directory.Parent)
            if (File.Exists(Path.Combine(directory.FullName, "Tools/Distribution/PackageEngine.ps1")))
                return directory.FullName;
        throw new DirectoryNotFoundException("開発用Editorの「バージョン管理」から起動してください");
    }

    private static async Task AcceptActivations(string key, MainWindow window, CancellationToken token) {
        while (!token.IsCancellationRequested) {
            try {
                using var server = new NamedPipeServerStream(key, PipeDirection.In, 1, PipeTransmissionMode.Byte,
                    PipeOptions.Asynchronous | PipeOptions.CurrentUserOnly);
                await server.WaitForConnectionAsync(token).ConfigureAwait(false);
                using var reader = new StreamReader(server);
                var editor = await reader.ReadLineAsync(token).ConfigureAwait(false);
                await window.Dispatcher.InvokeAsync(() => window.ActivateFromEditor(editor));
            } catch (OperationCanceledException) { return; } catch (IOException) { await Task.Delay(100, token).ConfigureAwait(false); }
        }
    }
}
