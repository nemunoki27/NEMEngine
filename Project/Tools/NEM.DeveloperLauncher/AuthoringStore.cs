using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine.DeveloperLauncher;

internal sealed record SourceEntry(string Id, string Name, string Directory, JsonObject Data) {
    public override string ToString() => Name;
}

internal sealed class AuthoringStore(string repository) {
    private string Folder(string kind) => Path.Combine(repository, "Templates",
        kind switch { "Sample" => "Samples", "Asset" => "Assets", _ => "ManagedProject" });

    internal IReadOnlyList<SourceEntry> List(string kind) {
        var folder = Folder(kind);
        if (!Directory.Exists(folder))
            return [];
        var entries = new List<SourceEntry>();
        foreach (var directory in Directory.EnumerateDirectories(folder)) {
            var id = Path.GetFileName(directory);
            if (kind is "Sample" or "Asset" ? !Guid.TryParseExact(id, "N", out _) : id is not ("2D" or "3D"))
                continue;
            var path = Path.Combine(directory, kind == "Asset" ? "asset.json" : "template.json");
            if (!File.Exists(path))
                continue;
            CheckPath(path);
            var data = DeveloperSession.Read(path);
            if (DeveloperSession.Text(data, "id") != id || (kind != "Asset" && DeveloperSession.Text(data, "kind") != (kind == "Sample" ? kind : id)))
                throw new InvalidDataException("配布元の識別子が一致しません: " + path);
            data["version"] ??= "1.0.0";
            entries.Add(new(id, DeveloperSession.Text(data, "name"), directory, data));
        }
        return entries.OrderBy(e => e.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
    }

    internal void SaveSample(SourceEntry entry, string name, string description, string version, string engineVersion) {
        if (string.IsNullOrWhiteSpace(name))
            throw new ArgumentException("名前を入力してください");
        DeveloperSession.RequireVersion(version);
        DeveloperSession.RequireVersion(engineVersion);
        var path = Path.Combine(entry.Directory, "template.json");
        CheckPath(path);
        var data = DeveloperSession.Read(path);
        data["name"] = name;
        data["description"] = description;
        data["version"] = version;
        data["engineVersion"] = engineVersion;
        WriteJson(path, data);
    }

    internal async Task<SourceEntry> SaveAsset(string id, string name, string description, string version, string engine, string source) {
        if (!Guid.TryParseExact(id, "N", out _))
            throw new InvalidDataException("配布元の識別子が不正です");
        if (string.IsNullOrWhiteSpace(name))
            throw new ArgumentException("名前を入力してください");
        DeveloperSession.RequireVersion(version);
        if (!string.IsNullOrWhiteSpace(engine))
            DeveloperSession.RequireVersion(engine);
        if (!File.Exists(source) || !string.Equals(Path.GetExtension(source), ".zip", StringComparison.OrdinalIgnoreCase))
            throw new FileNotFoundException("配布するZIPを選択してください");
        var directory = Path.Combine(Folder("Asset"), id);
        CheckPath(directory);
        Directory.CreateDirectory(directory);
        var archiveName = Path.GetFileName(source);
        var target = Path.Combine(directory, archiveName);
        CheckPath(target);
        if (!Path.GetFullPath(source).Equals(target, StringComparison.OrdinalIgnoreCase)) {
            var temp = target + "." + Guid.NewGuid().ToString("N") + ".writing";
            try {
                await using (var input = File.OpenRead(source))
                await using (var output = File.Open(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                    await input.CopyToAsync(output);
                File.Move(temp, target, true);
            } finally { File.Delete(temp); }
        }
        var data = new JsonObject {
            ["schemaVersion"] = 1,
            ["id"] = id,
            ["name"] = name,
            ["description"] = description,
            ["version"] = version,
            ["recommendedEngineVersion"] = engine,
            ["archivePath"] = archiveName,
            ["fileName"] = archiveName
        };
        var metadata = Path.Combine(directory, "asset.json");
        CheckPath(metadata);
        WriteJson(metadata, data);
        return new(id, name, directory, data);
    }

    internal void RemoveSample(SourceEntry entry) {
        if (!Guid.TryParseExact(entry.Id, "N", out _))
            throw new InvalidDataException("サンプルの識別子が不正です");
        var target = Path.Combine(Folder("Sample"), entry.Id);
        CheckPath(target);
        if (!Path.GetFullPath(target).Equals(entry.Directory, StringComparison.OrdinalIgnoreCase) ||
            DeveloperSession.Text(DeveloperSession.Read(Path.Combine(target, "template.json")), "id") != entry.Id)
            throw new InvalidDataException("削除する保存元が一致しません");
        Directory.Delete(target, true);
    }

    private void CheckPath(string path) {
        var root = Path.Combine(repository, "Templates");
        for (var part = Path.GetFullPath(path); ; part = Path.GetDirectoryName(part)!) {
            if (File.Exists(part) || Directory.Exists(part))
                if ((File.GetAttributes(part) & FileAttributes.ReparsePoint) != 0)
                    throw new IOException("リンクされた配布元を変更できません");
            if (string.Equals(part, root, StringComparison.OrdinalIgnoreCase))
                return;
            if (!part.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                throw new IOException("配布元の保存先が不正です");
        }
    }

    private static void WriteJson(string path, JsonObject data) {
        string temp = path + "." + Guid.NewGuid().ToString("N") + ".writing";
        try {
            File.WriteAllText(temp, data.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + Environment.NewLine);
            File.Move(temp, path, true);
        } finally { File.Delete(temp); }
    }
}
