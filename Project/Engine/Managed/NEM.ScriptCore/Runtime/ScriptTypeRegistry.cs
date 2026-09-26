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
    internal Dictionary<Type, object?> defaultInstanceCache = new();

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

            // field map を作りつつ defaultValueJson を埋める
            object? defaults = CreateDefaultInstance(entry.type);
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
                    string? normalizedID = ScriptGeneratedMetadata.NormalizeGuid(fieldID);
                    if (info == null || normalizedID != fieldID || info.IsStatic || info.IsInitOnly || info.IsLiteral ||
                        !registeredFields.Add(info) || !entry.fieldMap.TryAdd(fieldID, info)) {
                        throw new InvalidOperationException($"Invalid or duplicate field: {entry.fullTypeName}.{fieldName}");
                    }
                    {

                        if (ScriptFieldCodec.IsUnsupportedField(fieldObj)) {
                            entry.unsupportedFields.Add(fieldID);
                        }
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
                        ? "null" : codec.SerializeFieldDefault(info, defaults, defaultGraph);
                }
            }

            entry.schemaJson = typeNode.ToJsonString();
        }
    }

    internal void AddScriptTypeEntry(string rawGuid, Type type, string fullName, string displayName,
        string sourcePath, bool hasExplicitID) {

        string? normalized = ScriptGeneratedMetadata.NormalizeGuid(rawGuid);
        if (normalized == null || guidToEntry.ContainsKey(normalized) || typeToEntry.ContainsKey(type)) {
            throw new InvalidOperationException($"Invalid or duplicate Script Type GUID: {rawGuid}, type: {fullName}");
        }
        if (!typeof(MonoBehaviour).IsAssignableFrom(type) || type.IsAbstract || type.ContainsGenericParameters) {
            throw new InvalidOperationException($"Invalid MonoBehaviour type: {fullName}");
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
            callbacks = new ScriptCallbacks(type),
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
        object instance = Activator.CreateInstance(type)
            ?? throw new InvalidOperationException($"Failed to create default instance: {type.FullName}");
        defaultInstanceCache[type] = instance;
        return instance;
    }
}
