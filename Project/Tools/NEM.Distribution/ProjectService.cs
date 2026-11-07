using System.Diagnostics;
using System.Text.Json.Nodes;
using System.Xml.Linq;

namespace NEMEngine.Distribution;

// プロジェクトの生成とPC固有の参照を管理する
public sealed partial class ProjectService(LauncherStore store) {
    public static ProjectEntry Read(string descriptor) {
        descriptor = Path.GetFullPath(descriptor);
        if (Path.GetExtension(descriptor) != ".nemproject") throw new InvalidDataException(".nemprojectを選択してください");
        var data = JsonNode.Parse(File.ReadAllText(descriptor))?.AsObject()
            ?? throw new InvalidDataException("プロジェクトを読み込めません");
        string version = data["engineVersion"]?.GetValue<string>() ?? "";
        DistributionStorage.RequireVersion(version);
        if (data["schemaVersion"]?.GetValue<int>() != 1 || !Guid.TryParse(data["projectGuid"]?.GetValue<string>(), out _))
            throw new InvalidDataException("対応するプロジェクトの形式ではありません");
        return new() { ProjectGUID = Guid.Parse(data["projectGuid"]!.GetValue<string>()).ToString("N"),
            Name = data["name"]?.GetValue<string>() ?? Path.GetFileNameWithoutExtension(descriptor),
            Descriptor = descriptor, EngineVersion = version };
    }

    public ProjectEntry Create(string name, string parent, string version, string template, CancellationToken token = default) {
        if (template is not ("2D" or "3D")) throw new ArgumentException("2Dまたは3Dを選択してください");
        var installed = store.Resolve(version);
        string source = Path.Combine(installed.Root, "ProjectTemplates", template);
        return CreateFromTemplate(name, parent, version, source, template, null, token);
    }

    public static void Bind(string descriptor, InstalledEngine engine) {
        var project = Read(descriptor);
        var manifest = EngineValidation.Read(engine.Root);
        if (project.EngineVersion != manifest.Version || engine.Version != manifest.Version)
            throw new InvalidDataException("指定されたエンジンと使用版が一致しません");
        if (!File.Exists(Path.Combine(engine.ToolchainRoot, "dotnet.exe"))) throw new IOException("C#ツールチェーンを修復してください");
        string root = Path.GetDirectoryName(project.Descriptor)!;
        string bindings = Path.Combine(root, "Library", "EngineBinding.props");
        Directory.CreateDirectory(Path.GetDirectoryName(bindings)!);
        var document = new XDocument(new XElement("Project", new XElement("PropertyGroup",
            new XElement("NEMEngineRoot", Path.GetFullPath(engine.Root)),
            new XElement("NEMToolchainRoot", Path.GetFullPath(engine.ToolchainRoot)))));
        DistributionStorage.WriteText(bindings, document.ToString());
        string solution = Path.Combine(root, Path.GetFileNameWithoutExtension(descriptor) + ".slnx");
        DistributionStorage.WriteText(solution, new XDocument(new XElement("Solution",
            new XElement("Project", new XAttribute("Path", "Scripts/GameScripts.csproj")))).ToString());
        // VisualStudioのSDK探索も同じ配布版へ固定する
        DistributionStorage.Write(Path.Combine(root, "global.json"), new {
            sdk = new { version = manifest.ToolchainVersion, rollForward = "disable", allowPrerelease = false,
                paths = new[] { Path.GetFullPath(engine.ToolchainRoot) } }
        });
    }

    public int StartEditor(ProjectEntry project) {
        if (IsRunning(project.Descriptor)) {
            using var session = new FileStream(Path.Combine(Path.GetDirectoryName(project.Descriptor)!, "Library", "EditorSession.lock"),
                FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            using var reader = new StreamReader(session);
            if (int.TryParse(reader.ReadToEnd(), out int processID)) {
                using var existing = Process.GetProcessById(processID);
                WindowActivation.Focus(existing.MainWindowHandle);
                return processID;
            }
            throw new IOException("このプロジェクトは既に起動しています");
        }
        var engine = store.Resolve(project.EngineVersion);
        Bind(project.Descriptor, engine);
        var manifest = EngineValidation.Read(engine.Root);
        var start = new ProcessStartInfo(DistributionStorage.Child(engine.Root, manifest.Editor)) {
            UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(project.Descriptor)!
        };
        foreach (string argument in new[] { "--project", project.Descriptor, "--engine-root", engine.Root, "--toolchain-root", engine.ToolchainRoot })
            start.ArgumentList.Add(argument);
        start.Environment["NEMENGINE_ROOT"] = engine.Root;
        start.Environment["NEMENGINE_TOOLCHAIN_ROOT"] = engine.ToolchainRoot;
        start.Environment["DOTNET_ROOT"] = engine.ToolchainRoot;
        start.Environment["DOTNET_HOST_PATH"] = Path.Combine(engine.ToolchainRoot, "dotnet.exe");
        // ゲーム制作に不要な.NETの初回処理を抑える
        start.Environment["DOTNET_CLI_TELEMETRY_OPTOUT"] = "1";
        start.Environment["DOTNET_GENERATE_ASPNET_CERTIFICATE"] = "false";
        start.Environment["DOTNET_NOLOGO"] = "1";
        start.Environment["PATH"] = engine.ToolchainRoot + Path.PathSeparator + Environment.GetEnvironmentVariable("PATH");
        using var process = Process.Start(start) ?? throw new IOException("Editorを起動できません");
        store.Register(project with { LastOpened = DateTime.UtcNow });
        return process.Id;
    }

    public static bool IsRunning(string descriptor) {
        string session = Path.Combine(Path.GetDirectoryName(Path.GetFullPath(descriptor))!, "Library", "EditorSession.lock");
        if (!File.Exists(session)) return false;
        try { using var file = File.Open(session, FileMode.Open, FileAccess.ReadWrite, FileShare.None); return false; }
        catch (IOException) { return true; }
    }

    public void ChangeVersion(ProjectEntry project, string version) {
        if (IsRunning(project.Descriptor)) throw new IOException("保存してEditorを終了してから使用版を変更してください");
        var target = store.Resolve(version);
        EngineValidation.Read(target.Root);
        string root = Path.GetDirectoryName(project.Descriptor)!;
        string parent = Path.GetDirectoryName(root)!;
        string backup = DistributionStorage.Child(parent, Path.GetFileName(root) + ".backup-" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff"));
        Directory.CreateDirectory(backup);
        // キャッシュとGit管理情報をバックアップへ混ぜない
        foreach (string entry in Directory.EnumerateFileSystemEntries(root)) {
            string name = Path.GetFileName(entry);
            if (name is "Library" or "Managed" or "Saved" or "Generated" or ".git" or ".vs" or "global.json" || name.EndsWith(".slnx")) continue;
            if (Directory.Exists(entry)) DistributionStorage.CopyTree(entry, Path.Combine(backup, name));
            else File.Copy(entry, Path.Combine(backup, name));
        }
        var data = JsonNode.Parse(File.ReadAllText(project.Descriptor))!.AsObject();
        string previous = data["engineVersion"]!.GetValue<string>();
        data["engineVersion"] = version;
        DistributionStorage.Write(project.Descriptor, data);
        try {
            Bind(project.Descriptor, target);
            store.Register(Read(project.Descriptor) with { LastOpened = project.LastOpened });
        } catch (Exception failure) {
            try {
                data["engineVersion"] = previous;
                DistributionStorage.Write(project.Descriptor, data);
                Bind(project.Descriptor, store.Resolve(previous));
            } catch (Exception recovery) {
                throw new AggregateException("使用版を復元できません バックアップを確認してください: " + backup, failure, recovery);
            }
            throw;
        }
    }
}
