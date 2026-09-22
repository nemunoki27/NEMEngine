using System.Reflection;





using System.Text.Json.Nodes;



namespace NEMEngine;

// Assemblyの型とスキーマを登録する
internal sealed unsafe class ScriptTypeRegistry {

    internal readonly List<ScriptTypeEntry> scriptTypeEntries = new();
    // Stable GUID -> entry（instance 作成・field 取得の解決に使う）
    internal readonly Dictionary<string, ScriptTypeEntry> guidToEntry = new(StringComparer.Ordinal);
    // Type -> entry（runtime instance から schema / field map を引く）
    internal readonly Dictionary<Type, ScriptTypeEntry> typeToEntry = new();
    // authoring default 抽出用の一時 instance キャッシュ（型ごと。reload で破棄）
    internal readonly Dictionary<Type, object?> defaultInstanceCache = new();

    internal Assembly? gameAssembly;

    internal void RebuildScriptTypes(ScriptFieldCodec codec) {

        scriptTypeEntries.Clear();
        guidToEntry.Clear();
        typeToEntry.Clear();
        defaultInstanceCache.Clear();
        // 旧assemblyのinstanceを指す保留参照はreloadで無効になるため破棄する
        codec.pendingReferences.Clear();

        if (gameAssembly == null) {
            return;
        }

        // 生成 registryから型を登録する
        ScriptTypeDescriptor[]? generated = ScriptGeneratedMetadata.TryReadGeneratedManifest(gameAssembly);
        if (generated == null) {
            NativeApplicationAPI.WriteLog(2,
                "GeneratedScriptManifest was not found. Add the NEM.ScriptCodeGen analyzer to GameScripts.");
            return;
        }
        foreach (ScriptTypeDescriptor descriptor in generated) {

            Type? type = gameAssembly.GetType(descriptor.FullTypeName, throwOnError: false);
            if (type == null) {
                NativeApplicationAPI.WriteLog(2, $"Generated manifest type not found in assembly: {descriptor.FullTypeName}");
                continue;
            }
            AddScriptTypeEntry(descriptor.ScriptTypeID, type, descriptor.FullTypeName,
                descriptor.DisplayName, descriptor.SourcePath, descriptor.HasExplicitID);
        }

        // 表示・登録順を安定させる（full type name 昇順）
        scriptTypeEntries.Sort((a, b) => string.CompareOrdinal(a.fullTypeName, b.fullTypeName));

        // 型登録が確定したので serialized field schema と field map を一度だけ構築する（hot path 外）
        if (!BuildSchemaRegistry(codec)) {
            scriptTypeEntries.Clear();
            guidToEntry.Clear();
            typeToEntry.Clear();
            return;
        }

        if (scriptTypeEntries.Count == 0) {
            NativeApplicationAPI.WriteLog(1, "GameScripts loaded, but no ScriptBehaviour types were found.");
        }
    }

    internal bool BuildSchemaRegistry(ScriptFieldCodec codec) {

        JsonObject? generatedByType = ScriptGeneratedMetadata.TryReadGeneratedSchema(gameAssembly!);
        if (generatedByType == null) {
            NativeApplicationAPI.WriteLog(2,
                "GeneratedScriptSchema was not found. Add the NEM.ScriptCodeGen analyzer to GameScripts.");
            return false;
        }

        foreach (ScriptTypeEntry entry in scriptTypeEntries) {

            JsonObject? typeNode = null;
            if (generatedByType != null && generatedByType.TryGetPropertyValue(entry.scriptTypeID, out JsonNode? n) && n is JsonObject obj) {
                typeNode = obj;
            }

            if (typeNode == null) {
                NativeApplicationAPI.WriteLog(2, $"Generated script schema was not found: {entry.fullTypeName}");
                return false;
            }

            // field map を作りつつ defaultValueJson を埋める
            object? defaults = CreateDefaultInstance(entry.type);
            if (typeNode["fields"] is JsonArray fields) {
                foreach (JsonNode? fieldNode in fields) {
                    if (fieldNode is not JsonObject fieldObj) {
                        continue;
                    }
                    string fieldID = fieldObj["fieldId"]?.GetValue<string>() ?? string.Empty;
                    string fieldName = fieldObj["name"]?.GetValue<string>() ?? string.Empty;
                    string declaringType = fieldObj["declaringType"]?.GetValue<string>() ?? string.Empty;
                    FieldInfo? info = ScriptGeneratedMetadata.ResolveFieldInfo(entry.type, declaringType, fieldName);
                    if (info != null && !string.IsNullOrEmpty(fieldID)) {
                        entry.fieldMap[fieldID] = info;
                        if (ScriptFieldCodec.CanReadRuntimeField(fieldObj)) {
                            entry.runtimeFieldMap[fieldID] = info;
                        }
                        // 参照解決を伴うフィールドは適用を遅延させる([SerializeReference]は候補型に参照が含まれ得る)
                        if (info.GetCustomAttribute<SerializeReferenceAttribute>() != null ||
                            ScriptFieldCodec.IsDeferredReferenceType(info.FieldType, null)) {
                            entry.deferredFields.Add(fieldID);
                        }
                    }
                    // 既定値（authoring 未設定時の初期値）を埋める
                    fieldObj["defaultValueJson"] = ScriptFieldCodec.IsUnsupportedField(fieldObj)
                        ? "null" : codec.SerializeFieldDefault(info, defaults);
                }
            }

            entry.schemaJson = typeNode.ToJsonString();
        }
        return true;
    }

    internal void AddScriptTypeEntry(string rawGuid, Type type, string fullName, string displayName,
        string sourcePath, bool hasExplicitID) {

        string? normalized = ScriptGeneratedMetadata.NormalizeGuid(rawGuid);
        if (normalized == null) {
            NativeApplicationAPI.WriteLog(2, $"Invalid Script Type GUID for '{fullName}'.");
            return;
        }

        if (guidToEntry.ContainsKey(normalized)) {

            // 重複 GUID。最初の型を維持し、後続は登録しない（manifest validation でも検出する）
            NativeApplicationAPI.WriteLog(2,
                $"Duplicate Script Type GUID '{normalized}' for '{fullName}'. Skipping the duplicate registration.");
            return;
        }

        // [DefaultExecutionOrder] を load 時に一度だけ反射で読む（hot path では参照しない）。
        // 値は native の registry まで流れ、Editor override が無いときの default order になる。
        int defaultExecutionOrder = 0;
        DefaultExecutionOrderAttribute? orderAttribute = type.GetCustomAttribute<DefaultExecutionOrderAttribute>();
        if (orderAttribute != null) {
            defaultExecutionOrder = orderAttribute.Order;
        }

        var entry = new ScriptTypeEntry {
            scriptTypeID = normalized,
            type = type,
            fullTypeName = fullName,
            displayName = string.IsNullOrEmpty(displayName) ? type.Name : displayName,
            sourcePath = sourcePath ?? string.Empty,
            hasExplicitID = hasExplicitID,
            defaultExecutionOrder = defaultExecutionOrder,
        };
        scriptTypeEntries.Add(entry);
        guidToEntry[normalized] = entry;
        typeToEntry[type] = entry;
    }

    internal bool TryGetEntry(string? scriptTypeID, out ScriptTypeEntry entry) {

        entry = null!;
        string? normalized = ScriptGeneratedMetadata.NormalizeGuid(scriptTypeID);
        if (normalized == null) {
            return false;
        }
        return guidToEntry.TryGetValue(normalized, out entry!);
    }

    internal bool TryGetFieldInfo(Type type, string fieldID, out FieldInfo field) {

        field = null!;
        if (typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) &&
            entry.fieldMap.TryGetValue(fieldID, out FieldInfo? info)) {
            field = info;
            return true;
        }
        return false;
    }

    internal object? CreateDefaultInstance(Type type) {

        if (defaultInstanceCache.TryGetValue(type, out object? cached)) {
            return cached;
        }
        object? instance = null;
        try {
            instance = Activator.CreateInstance(type);
        }
        catch (Exception ex) {
            NativeApplicationAPI.WriteLog(1, $"Failed to create default instance for '{type.FullName}': {ex.Message}");
        }
        defaultInstanceCache[type] = instance;
        return instance;
    }
}
