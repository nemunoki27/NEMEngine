using System.Reflection;
using System.Text.Json.Nodes;

namespace NEMEngine;

// Assemblyの型とスキーマを登録する
internal sealed unsafe class ScriptTypeRegistry {

    internal List<ScriptTypeEntry> scriptTypeEntries = new();
    // 保存IDから型を解決する
    internal Dictionary<string, ScriptTypeEntry> guidToEntry = new(StringComparer.Ordinal);
    // 実行型からField情報を解決する
    internal Dictionary<Type, ScriptTypeEntry> typeToEntry = new();
    // 編集用の既定値を型ごとに保持する
    internal Dictionary<Type, object> defaultInstanceCache = new();

    internal Assembly? gameAssembly;

    // 全候補を検証してから登録結果を公開する
    internal void RebuildScriptTypes(ScriptFieldCodec codec) {

        Assembly assembly = gameAssembly ?? throw new InvalidOperationException("GameScripts assembly is not loaded.");
        ScriptTypeDescriptor[] generated = ScriptGeneratedMetadata.TryReadGeneratedManifest(assembly)
            ?? throw new InvalidOperationException("GeneratedScriptManifest was not found.");
        JsonObject schema = ScriptGeneratedMetadata.TryReadGeneratedSchema(assembly)
            ?? throw new InvalidOperationException("GeneratedScriptSchema was not found.");
        RegisterScriptTypes(assembly, generated, schema, codec);
    }

    // 読込候補と公開済みの登録を分ける
    internal void RegisterScriptTypes(Assembly assembly, ScriptTypeDescriptor[] generated, JsonObject schema,
        ScriptFieldCodec codec) {

        var candidate = new ScriptTypeRegistry { gameAssembly = assembly };
        var candidateCodec = new ScriptFieldCodec(candidate);
        foreach (ScriptTypeDescriptor descriptor in generated) {
            Type type = ScriptGeneratedMetadata.ResolveType(assembly, descriptor.FullTypeName)
                ?? throw new InvalidOperationException($"Generated manifest type not found: {descriptor.FullTypeName}");
            candidate.AddScriptTypeEntry(descriptor.ScriptTypeID, type, descriptor.FullTypeName,
                descriptor.DisplayName, descriptor.SourcePath, descriptor.HasExplicitID);
        }
        candidate.scriptTypeEntries.Sort((a, b) => string.CompareOrdinal(a.fullTypeName, b.fullTypeName));
        candidate.BuildSchemaRegistry(candidateCodec, schema.DeepClone().AsObject());

        // 型とFieldの対応を一組で差し替える
        codec.ResetAssemblyState();
        scriptTypeEntries = candidate.scriptTypeEntries;
        guidToEntry = candidate.guidToEntry;
        typeToEntry = candidate.typeToEntry;
        defaultInstanceCache = candidate.defaultInstanceCache;
        gameAssembly = assembly;
    }

    // 型IDと通知先と実行順序を登録する
    internal void AddScriptTypeEntry(string rawGUID, Type type, string fullName, string displayName,
        string sourcePath, bool hasExplicitID) {

        string? normalized = ScriptGeneratedMetadata.NormalizeGUID(rawGUID);
        if (normalized == null || guidToEntry.ContainsKey(normalized) || typeToEntry.ContainsKey(type)) {
            throw new InvalidOperationException($"Invalid or duplicate Script Type GUID: {rawGUID}, type: {fullName}");
        }
        if (!typeof(MonoBehaviour).IsAssignableFrom(type) || type.IsAbstract || type.ContainsGenericParameters) {
            throw new InvalidOperationException($"Invalid MonoBehaviour type: {fullName}");
        }

        // 型登録時に既定の実行順序を取得する
        int defaultExecutionOrder = 0;
        DefaultExecutionOrderAttribute? orderAttribute = type.GetCustomAttribute<DefaultExecutionOrderAttribute>();
        if (orderAttribute != null) {
            defaultExecutionOrder = orderAttribute.Order;
        }

        var entry = new ScriptTypeEntry(type) {
            scriptTypeID = normalized,
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

    // 正規化した保存IDから型情報を返す
    internal bool TryGetEntry(string? scriptTypeID, out ScriptTypeEntry entry) {

        entry = null!;
        string? normalized = ScriptGeneratedMetadata.NormalizeGUID(scriptTypeID);
        if (normalized == null) {
            return false;
        }
        return guidToEntry.TryGetValue(normalized, out entry!);
    }

    // 登録型の保存Fieldを解決する
    internal bool TryGetFieldInfo(Type type, string fieldID, out FieldInfo field) {

        field = null!;
        if (typeToEntry.TryGetValue(type, out ScriptTypeEntry? entry) &&
            entry.fieldMap.TryGetValue(fieldID, out FieldInfo? info)) {
            field = info;
            return true;
        }
        return false;
    }

    // 型ごとに初期値の取得元を保持する
    internal object CreateDefaultInstance(Type type) {

        if (defaultInstanceCache.TryGetValue(type, out object? cached)) {
            return cached;
        }
        object instance = Activator.CreateInstance(type)
            ?? throw new InvalidOperationException($"Failed to create default instance: {type.FullName}");
        defaultInstanceCache[type] = instance;
        return instance;
    }

    // Fieldの対応と参照遅延と保存schemaを確定する
    private void BuildSchemaRegistry(ScriptFieldCodec codec, JsonObject generatedByType) {

        if (generatedByType.Count != scriptTypeEntries.Count) {
            throw new InvalidOperationException("Generated manifest and schema contain different script types.");
        }
        foreach (ScriptTypeEntry entry in scriptTypeEntries) {

            JsonObject? typeNode = null;
            if (generatedByType.TryGetPropertyValue(entry.scriptTypeID, out JsonNode? n) && n is JsonObject obj) {
                typeNode = obj;
            }

            if (typeNode == null) {
                throw new InvalidOperationException($"Generated script schema was not found: {entry.fullTypeName}");
            }

            // Fieldの対応と既定値を作成する
            object defaults = CreateDefaultInstance(entry.type);
            ScriptReferenceGraph defaultGraph = codec.CreateReferenceGraph(entry.type.Assembly);
            if (typeNode["fields"] is not JsonArray fields) {
                throw new InvalidOperationException($"Missing field schema: {entry.fullTypeName}");
            }
            var registeredFields = new HashSet<FieldInfo>();
            {
                foreach (JsonNode? fieldNode in fields) {
                    if (fieldNode is not JsonObject fieldObj) {
                        throw new InvalidOperationException($"Invalid field schema: {entry.fullTypeName}");
                    }
                    string fieldID = fieldObj["fieldId"]?.GetValue<string>() ?? string.Empty;
                    string fieldName = fieldObj["name"]?.GetValue<string>() ?? string.Empty;
                    string declaringType = fieldObj["declaringType"]?.GetValue<string>() ?? string.Empty;
                    FieldInfo? info = ScriptGeneratedMetadata.ResolveFieldInfo(entry.type, declaringType, fieldName);
                    string? normalizedID = ScriptGeneratedMetadata.NormalizeGUID(fieldID);
                    if (info == null || normalizedID != fieldID || info.IsStatic || info.IsInitOnly || info.IsLiteral ||
                        !registeredFields.Add(info) || !entry.fieldMap.TryAdd(fieldID, info)) {
                        throw new InvalidOperationException($"Invalid or duplicate field: {entry.fullTypeName}.{fieldName}");
                    }
                    {

                        if (ScriptFieldTypeUtility.IsUnsupportedField(fieldObj)) {
                            entry.unsupportedFields.Add(fieldID);
                        }
                        if (ScriptFieldTypeUtility.CanReadRuntimeField(fieldObj)) {
                            entry.runtimeFieldMap[fieldID] = info;
                        }
                        // 個体生成後に参照を解決するFieldを登録する
                        if (info.GetCustomAttribute<SerializeReferenceAttribute>() != null ||
                            ScriptFieldTypeUtility.IsDeferredReferenceType(info.FieldType, null)) {
                            entry.deferredFields.Add(fieldID);
                        }
                    }
                    // 未設定時の初期値を保存schemaへ渡す
                    fieldObj["defaultValueJson"] = ScriptFieldTypeUtility.IsUnsupportedField(fieldObj)
                        ? "null" : codec.SerializeFieldDefault(info, defaults, defaultGraph);
                }
            }

            entry.schemaJSON = typeNode.ToJsonString();
        }
    }
}
