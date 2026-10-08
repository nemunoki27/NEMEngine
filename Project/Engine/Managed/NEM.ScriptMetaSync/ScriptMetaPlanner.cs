using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptMetaSync;

// スクリプトmetaの対応付け
internal sealed class ScriptMetaPlanner {

    private readonly ScriptMetaDiagnostics diagnostics;

    internal ScriptMetaPlanner(ScriptMetaDiagnostics diagnostics) { this.diagnostics = diagnostics; }

    internal JsonObject Plan(string csPath, JsonObject meta, List<ScriptModel> models, Mode mode,
        Dictionary<string, string> seenScriptIDs) {

        string metaPath = csPath + ".meta";

        var existingScripts = meta["scripts"] as JsonArray ?? new JsonArray();
        var newScripts = new JsonArray();
        var matchedExisting = new HashSet<JsonObject>();

        // 1. fullTypeName 一致 / 単純 rename で既存 script entry を対応付ける
        foreach (ScriptModel model in models) {

            JsonObject? existing = FindScriptByName(existingScripts, model.FullTypeName, matchedExisting);
            string? renamedFrom = null;
            if (existing == null) {
                // [FormerlyKnownScriptType] で旧名照合
                foreach (string former in model.FormerlyKnown) {
                    existing = FindScriptByName(existingScripts, former, matchedExisting);
                    if (existing != null) { renamedFrom = former; break; }
                }
            }

            if (existing != null) { matchedExisting.Add(existing); }
            newScripts.Add(BuildScriptEntry(model, existing, renamedFrom, mode, seenScriptIDs));
        }

        // 2. source から消えた script type の単純 rename 推定（1 消失 + 1 新規・既存と未対応）
        ResolveScriptRenames(csPath, existingScripts, matchedExisting, models, newScripts, mode);

        // 3. 未対応の既存 script は破壊せず保持する（値の round-trip のため）
        foreach (JsonNode? node in existingScripts) {
            if (node is JsonObject obj && !matchedExisting.Contains(obj)) {
                Console.WriteLine($"[ScriptMetaSync] keep orphan script entry '{obj["fullTypeName"]}' in {Path.GetFileName(metaPath)} (source not found).");
                newScripts.Add(obj.DeepClone());
            }
        }

        // scriptTypeID で安定ソート
        var sorted = newScripts.OfType<JsonObject>().OrderBy(s => (string?)s["scriptTypeId"] ?? "", StringComparer.Ordinal).ToList();
        var sortedArray = new JsonArray();
        foreach (JsonObject s in sorted) { sortedArray.Add(s.DeepClone()); }

        meta["scripts"] = sortedArray;

        return meta;
    }

    internal JsonObject? FindScriptByName(JsonArray scripts, string fullTypeName, HashSet<JsonObject> used) {
        foreach (JsonNode? node in scripts) {
            if (node is JsonObject obj && !used.Contains(obj) &&
                (string?)obj["fullTypeName"] == fullTypeName) {
                return obj;
            }
        }
        return null;
    }

    internal void ResolveScriptRenames(string csPath, JsonArray existingScripts,
        HashSet<JsonObject> matchedExisting, List<ScriptModel> models, JsonArray newScripts, Mode mode) {

        // newScripts のうち、scriptTypeID が新規採番された(=既存対応が無かった)もの
        var unresolvedNew = newScripts.OfType<JsonObject>()
            .Where(s => s["__isNew"]?.GetValue<bool>() == true).ToList();
        var unmatchedExisting = existingScripts.OfType<JsonObject>()
            .Where(s => !matchedExisting.Contains(s)).ToList();

        if (unresolvedNew.Count == 1 && unmatchedExisting.Count == 1) {

            // 安全な単純 rename：旧 scriptTypeID を維持し formerNames へ旧名を追加
            JsonObject newEntry = unresolvedNew[0];
            JsonObject oldEntry = unmatchedExisting[0];
            string oldName = (string?)oldEntry["fullTypeName"] ?? "";
            string oldID = (string?)oldEntry["scriptTypeId"] ?? "";
            if (!string.IsNullOrEmpty(oldID)) {
                newEntry["scriptTypeId"] = oldID;
                AddFormerName(newEntry, oldName);
                MergeFieldsFromRenamedScript(newEntry, oldEntry);
                matchedExisting.Add(oldEntry);
                Console.WriteLine($"[ScriptMetaSync] script rename: '{oldName}' -> '{newEntry["fullTypeName"]}' (id kept).");
            }
        } else if (unresolvedNew.Count > 0 && unmatchedExisting.Count > 0) {

            // 曖昧：自動決定しない。診断を出して保留する
            diagnostics.ambiguousCount++;
            Console.Error.WriteLine($"[ScriptMetaSync] ambiguous script rename in {Path.GetFileName(csPath)}: " +
                $"new=[{string.Join(",", unresolvedNew.Select(s => (string?)s["fullTypeName"]))}] " +
                $"removed=[{string.Join(",", unmatchedExisting.Select(s => (string?)s["fullTypeName"]))}]. Resolve manually.");
        }

        // 内部マーカーを除去
        foreach (JsonObject s in newScripts.OfType<JsonObject>().ToList()) {
            s.Remove("__isNew");
        }
    }

    internal void MergeFieldsFromRenamedScript(JsonObject newEntry, JsonObject oldEntry) {
        // rename された型の field id を、名前一致で引き継ぐ
        if (oldEntry["fields"] is not JsonArray oldFields || newEntry["fields"] is not JsonArray newFields) {
            return;
        }
        foreach (JsonObject nf in newFields.OfType<JsonObject>()) {
            string name = (string?)nf["name"] ?? "";
            foreach (JsonObject of in oldFields.OfType<JsonObject>()) {
                if ((string?)of["name"] == name && of["fieldId"] != null) {
                    nf["fieldId"] = (string?)of["fieldId"];
                    break;
                }
            }
        }
    }

    internal JsonObject BuildScriptEntry(ScriptModel model, JsonObject? existing,
        string? renamedFrom, Mode mode, Dictionary<string, string> seenScriptIDs) {

        var entry = new JsonObject();
        entry["fullTypeName"] = model.FullTypeName;

        // scriptTypeID: explicit attribute → existing meta → 新規 UUID
        string? scriptID = diagnostics.NormalizeOrError(model.ExplicitID, $"{model.FullTypeName} [ScriptTypeID]");
        bool isNew = false;
        if (scriptID == null) {
            scriptID = (string?)existing?["scriptTypeId"];
            scriptID = diagnostics.NormalizeOrError(scriptID, $"{model.FullTypeName} meta scriptTypeId");
        }
        if (scriptID == null) {
            if (mode == Mode.ValidateOnly) {
                diagnostics.errorCount++;
                Console.Error.WriteLine($"[ScriptMetaSync] missing scriptTypeId for '{model.FullTypeName}'. Run Editor metadata sync.");
                scriptID = "00000000-0000-0000-0000-000000000000";
            } else {
                scriptID = Guid.NewGuid().ToString("D");
                isNew = true;
            }
        }

        // 全体での scriptTypeID 重複検出
        if (seenScriptIDs.TryGetValue(scriptID, out string? owner) && owner != model.FullTypeName) {
            diagnostics.errorCount++;
            Console.Error.WriteLine($"[ScriptMetaSync] duplicate scriptTypeId '{scriptID}' on '{model.FullTypeName}' and '{owner}'.");
        } else {
            seenScriptIDs[scriptID] = model.FullTypeName;
        }

        entry["scriptTypeId"] = scriptID;
        if (isNew) { entry["__isNew"] = true; }

        // formerNames（既存 + [FormerlyKnownScriptType] + rename 由来）
        var formerNames = new JsonArray();
        var formerSet = new HashSet<string>(StringComparer.Ordinal);
        void AddFormer(string? n) { if (!string.IsNullOrWhiteSpace(n) && formerSet.Add(n!)) { formerNames.Add(n!); } }
        if (existing?["formerNames"] is JsonArray ef) { foreach (JsonNode? n in ef) { AddFormer((string?)n); } }
        foreach (string fk in model.FormerlyKnown) { AddFormer(fk); }
        if (renamedFrom != null) { AddFormer(renamedFrom); }
        entry["formerNames"] = formerNames;

        // fields
        entry["fields"] = BuildFields(model, existing, mode);
        return entry;
    }

    internal JsonArray BuildFields(ScriptModel model, JsonObject? existingScript, Mode mode) {

        var existingFields = existingScript?["fields"] as JsonArray ?? new JsonArray();
        var usedExisting = new HashSet<JsonObject>();
        var seenFieldIDs = new Dictionary<string, string>(StringComparer.Ordinal);

        // 1) 名前 / formerNames / 明示 ID で対応付け
        var resolved = new List<(FieldModel model, JsonObject? existing, string? id)>();

        foreach (FieldModel f in model.Fields) {
            var names = new HashSet<string>(f.FormerlySerializedAs, StringComparer.Ordinal) { f.Name };
            string? explicitID = diagnostics.NormalizeOrError(f.ExplicitID, $"{model.FullTypeName}.{f.Name} [SerializedFieldID]");
            var candidates = existingFields.OfType<JsonObject>().Where(existing =>
                names.Contains((string?)existing["name"] ?? string.Empty) ||
                (existing["formerNames"] is JsonArray former && former.Any(name => names.Contains((string?)name ?? string.Empty))) ||
                (explicitID != null && Guid.TryParse((string?)existing["fieldId"], out Guid oldID) && oldID.ToString("D") == explicitID)).ToList();
            JsonObject? match = candidates.Count == 1 ? candidates[0] : null;
            string? matchedID = diagnostics.NormalizeOrError((string?)match?["fieldId"], $"{model.FullTypeName}.{f.Name} meta fieldId");
            if (candidates.Count > 1 || (match != null && !usedExisting.Add(match)) ||
                (explicitID != null && matchedID != null && explicitID != matchedID)) {
                ++diagnostics.errorCount;
                Console.Error.WriteLine($"[ScriptMetaSync] ambiguous field identity for '{model.FullTypeName}.{f.Name}'.");
            }
            string? id = explicitID ?? matchedID;
            resolved.Add((f, match, id));
        }

        // 明示的な対応がないFieldには新しいIDを割り当てる
        var fields = new JsonArray();
        foreach ((FieldModel f, JsonObject? existing, string? idIn) in resolved) {
            string? id = idIn;
            if (id == null) {
                if (mode == Mode.ValidateOnly) {
                    diagnostics.errorCount++;
                    Console.Error.WriteLine($"[ScriptMetaSync] missing fieldId for '{model.FullTypeName}.{f.Name}'. Run Editor metadata sync.");
                    id = "00000000-0000-0000-0000-000000000000";
                } else {
                    id = Guid.NewGuid().ToString("D");
                }
            }
            if (seenFieldIDs.TryGetValue(id, out string? owner)) {
                diagnostics.errorCount++;
                Console.Error.WriteLine($"[ScriptMetaSync] duplicate fieldId '{id}' on '{model.FullTypeName}.{f.Name}' and '.{owner}'.");
            } else {
                seenFieldIDs[id] = f.Name;
            }

            var fieldNode = new JsonObject {
                ["fieldId"] = id,
                ["name"] = f.Name,
                ["type"] = f.TypeText,
            };
            var former = new JsonArray();
            var formerSet = new HashSet<string>(StringComparer.Ordinal);
            void AddFormer(string? n) { if (!string.IsNullOrWhiteSpace(n) && n != f.Name && formerSet.Add(n!)) { former.Add(n!); } }
            if (existing?["formerNames"] is JsonArray ef) { foreach (JsonNode? n in ef) { AddFormer((string?)n); } }
            foreach (string s in f.FormerlySerializedAs) { AddFormer(s); }
            fieldNode["formerNames"] = former;
            fields.Add(fieldNode);
        }

        // 削除Fieldの保存値はScene側で旧GUIDのまま保持する

        // fieldID で安定ソート
        var sorted = fields.OfType<JsonObject>().OrderBy(f => (string?)f["fieldId"] ?? "", StringComparer.Ordinal).ToList();
        var sortedArray = new JsonArray();
        foreach (JsonObject f in sorted) { sortedArray.Add(f.DeepClone()); }
        return sortedArray;
    }

    internal void AddFormerName(JsonObject entry, string name) {
        if (string.IsNullOrWhiteSpace(name)) { return; }
        var arr = entry["formerNames"] as JsonArray ?? new JsonArray();
        if (!arr.OfType<JsonNode>().Any(n => (string?)n == name)) {
            arr.Add(name);
        }
        entry["formerNames"] = arr;
    }
}
