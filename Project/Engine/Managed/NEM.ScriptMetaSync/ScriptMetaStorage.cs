using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptMetaSync;

// スクリプトmetaの読込と保存
internal sealed class ScriptMetaStorage {

    private readonly ScriptMetaDiagnostics diagnostics;

    internal ScriptMetaStorage(ScriptMetaDiagnostics diagnostics) { this.diagnostics = diagnostics; }

    internal JsonObject LoadMeta(string metaPath) {
        try {
            if (File.Exists(metaPath)) {
                JsonNode? node = JsonNode.Parse(File.ReadAllText(metaPath));
                if (node is JsonObject obj) { return obj; }
            }
        }
        catch (Exception ex) {
            Console.Error.WriteLine($"[ScriptMetaSync] failed to read {metaPath}: {ex.Message}");
        }
        // .cs.meta が無ければ最小metaを作る
        return new JsonObject {
            ["schemaVersion"] = 2,
            ["guid"] = NewAssetGuid32(),
            ["type"] = "Script",
            ["importer"] = "ScriptImporter",
            ["importerVersion"] = 1,
            ["settings"] = new JsonObject(),
        };
    }

    internal string NewAssetGuid32() => Guid.NewGuid().ToString("N");

    internal bool WriteMetaIfChanged(string metaPath, JsonObject meta, Mode mode) {

        var options = new JsonSerializerOptions { WriteIndented = true };
        string serialized = meta.ToJsonString(options);

        string? current = File.Exists(metaPath) ? File.ReadAllText(metaPath) : null;
        if (current != null && NormalizeJson(current) == NormalizeJson(serialized)) {
            return false; // 変更なし（reload storm 防止）
        }

        if (mode == Mode.ValidateOnly) {
            diagnostics.errorCount++;
            Console.Error.WriteLine($"[ScriptMetaSync] {Path.GetFileName(metaPath)} is out of sync. Run Editor metadata sync.");
            return false;
        }

        // 一時ファイルへ書いてから置換（壊れた .meta を残さない）
        string tmp = metaPath + ".tmp";
        File.WriteAllText(tmp, serialized, new UTF8Encoding(false));
        File.Move(tmp, metaPath, overwrite: true);
        Console.WriteLine($"[ScriptMetaSync] updated {Path.GetFileName(metaPath)}");
        return true;
    }

    internal string NormalizeJson(string json) {
        try { return JsonNode.Parse(json)?.ToJsonString() ?? json; }
        catch { return json; }
    }
}
