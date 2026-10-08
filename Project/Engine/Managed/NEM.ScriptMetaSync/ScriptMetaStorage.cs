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
                if (node is JsonObject obj) { ValidateMetadata(obj); return obj; }
                throw new JsonException("Script meta must be a JSON object.");
            }
        }
        catch (Exception ex) {
            ++diagnostics.errorCount;
            Console.Error.WriteLine($"[ScriptMetaSync] failed to read {metaPath}: {ex.Message}");
            return new JsonObject();
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

    // 既存IDの不正を新規採番へ置き換えない
    internal static void ValidateMetadata(JsonObject meta) {
        if (!meta.ContainsKey("scripts")) { return; }
        if (meta["scripts"] is not JsonArray scripts) { throw new JsonException("Expected scripts array."); }
        var names = new HashSet<string>(StringComparer.Ordinal);
        var ids = new HashSet<Guid>();
        foreach (JsonNode? node in scripts) {
            if (node is not JsonObject script || script["fullTypeName"] is not JsonValue nameValue ||
                !nameValue.TryGetValue(out string? name) || string.IsNullOrWhiteSpace(name) || !names.Add(name) ||
                !Guid.TryParse((string?)script["scriptTypeId"], out Guid id) || !ids.Add(id)) {
                throw new JsonException("Invalid or duplicate script name/ID.");
            }
            ValidateFormerNames(script);
            if (!script.ContainsKey("fields")) { continue; }
            if (script["fields"] is not JsonArray fields) { throw new JsonException("Expected fields array: " + name); }
            var fieldNames = new HashSet<string>(StringComparer.Ordinal);
            var fieldIDs = new HashSet<Guid>();
            var aliases = new Dictionary<string, Guid>(StringComparer.Ordinal);
            foreach (JsonNode? fieldNode in fields) {
                if (fieldNode is not JsonObject field || field["name"] is not JsonValue fieldNameValue ||
                    !fieldNameValue.TryGetValue(out string? fieldName) || string.IsNullOrWhiteSpace(fieldName) || !fieldNames.Add(fieldName) ||
                    !Guid.TryParse((string?)field["fieldId"], out Guid fieldID) || !fieldIDs.Add(fieldID)) {
                    throw new JsonException("Invalid or duplicate field name/ID: " + name);
                }
                RegisterName(fieldName, fieldID);
                foreach (string former in ValidateFormerNames(field)) { RegisterName(former, fieldID); }
            }

            void RegisterName(string fieldName, Guid fieldID) {
                if (aliases.TryGetValue(fieldName, out Guid previous) && previous != fieldID) {
                    throw new JsonException("Ambiguous field name: " + name + "." + fieldName);
                }
                aliases[fieldName] = fieldID;
            }
        }
    }

    // 壊れた旧名情報を空の履歴として保存しない
    private static IReadOnlyList<string> ValidateFormerNames(JsonObject record) {
        if (!record.ContainsKey("formerNames")) { return Array.Empty<string>(); }
        if (record["formerNames"] is not JsonArray names) { throw new JsonException("Expected formerNames array."); }
        var result = new List<string>();
        foreach (JsonNode? node in names) {
            if (node is not JsonValue value || !value.TryGetValue(out string? name) || string.IsNullOrWhiteSpace(name)) {
                throw new JsonException("Invalid former field/type name.");
            }
            result.Add(name);
        }
        return result;
    }

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
