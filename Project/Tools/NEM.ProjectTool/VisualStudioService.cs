using System.Runtime.Loader;

namespace NEMEngine.ProjectTool;

internal sealed class VisualStudioService {
    private static string? interop_;

    static VisualStudioService() {
        // Interop本体はインストール済みVisual Studioから読み込む
        AssemblyLoadContext.Default.Resolving += (context, name) => {
            if (name.Name != "Microsoft.VisualStudio.Interop") return null;
            string path = interop_ ?? throw new IOException("Visual Studioの接続先が確定していません");
            if (!File.Exists(path)) throw new IOException("Visual StudioのC#開発環境を修復してください");
            Console.WriteLine("Visual Studio Interop: " + path);
            return context.LoadFromAssemblyPath(path);
        };
    }

    public Task OpenAsync(string solution, string? file, int line, int? editorPID) {
        var completion = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() => {
            try {
                using var session = VisualStudioSession.Open(solution);
                if (file != null) session.OpenScript(file, line);
                if (editorPID.HasValue) session.Attach(editorPID.Value);
                completion.SetResult();
            } catch (Exception error) { completion.SetException(error); }
        });
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        return completion.Task;
    }

    internal static void PrepareInterop(VisualStudioInstallation installation) {
        // 接続先と同じIDEのInteropを保持する
        interop_ = installation.Interop;
        Console.WriteLine("Visual Studio: " + installation.Executable + (installation.IsPrerelease ? " / Insiders" : " / Stable"));
    }
}
