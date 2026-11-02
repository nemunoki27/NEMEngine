using System.Windows;

namespace NEMEngine.Launcher;

internal static class Program {
    [STAThread]
    private static int Main(string[] args) {
        if (args.Length != 0 && args[0] is "--git-credential" or "--git-lfs")
            return GitHeadlessCommands.RunAsync(args).GetAwaiter().GetResult();
        var app = new App();
        app.InitializeComponent();
        return app.Run();
    }
}
