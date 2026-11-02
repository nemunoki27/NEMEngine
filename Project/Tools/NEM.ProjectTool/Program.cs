using System.Diagnostics;
using System.Runtime.InteropServices;
using NEMEngine.Distribution;

namespace NEMEngine.ProjectTool;

internal static class Program {
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int MessageBoxW(nint window, string text, string title, uint type);

    private static async Task<int> Main(string[] args) {
        Console.OutputEncoding = new System.Text.UTF8Encoding(false);
        try {
            if (args.Length == 0) throw new ArgumentException("create / bind / openを指定してください");
            var options = new Dictionary<string, string>(StringComparer.Ordinal);
            for (int index = 1; index < args.Length; ++index) {
                if (args[index] == "--ui") continue;
                if (!args[index].StartsWith("--") || index + 1 == args.Length) throw new ArgumentException("引数が不正です");
                options.Add(args[index][2..], args[++index]);
            }
            string Required(string key) => options.TryGetValue(key, out string? value) ? value : throw new ArgumentException("指定がありません: " + key);
            var store = new LauncherStore(options.GetValueOrDefault("state-root"));
            string root = Required("engine-root");
            var manifest = EngineValidation.Read(root);
            var engine = new InstalledEngine { Version = manifest.Version, Root = root, ToolchainRoot = Required("toolchain-root") };
            if (args[0] == "create") {
                store.Register(engine);
                var project = new ProjectService(store).Create(Required("name"), Required("parent"), engine.Version, Required("template"));
                Console.WriteLine(project.Descriptor);
                return 0;
            }
            string descriptor = Required("project");
            ProjectService.Bind(descriptor, engine);
            if (args[0] == "bind") return 0;
            string solution = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(descriptor))!, Path.GetFileNameWithoutExtension(descriptor) + ".slnx");
            int? pid = options.TryGetValue("editor-pid", out string? processID) ? int.Parse(processID) : null;
            if (pid.HasValue) {
                using var editor = Process.GetProcessById(pid.Value);
                string executable = editor.MainModule?.FileName ?? "";
                if (!Path.GetFullPath(executable).Equals(DistributionStorage.Child(root, manifest.Editor), StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("接続先が選択中エンジンのEditorではありません");
            }
            if (args[0] != "open") throw new ArgumentException("対応していない操作です");
            var studio = new VisualStudioService();
            await studio.OpenAsync(solution, options.GetValueOrDefault("file"),
                int.Parse(options.GetValueOrDefault("line", "1")), pid);
            return 0;
        } catch (Exception error) {
            Console.Error.WriteLine(error.Message);
            if (args.Contains("--ui")) MessageBoxW(0, error.Message, "NEMEngine", 0x10);
            return 1;
        }
    }
}
