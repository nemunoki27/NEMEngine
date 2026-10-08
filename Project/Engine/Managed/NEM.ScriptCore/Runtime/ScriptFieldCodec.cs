using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization.Metadata;

namespace NEMEngine;

// 保存値と実行値を変換し参照適用を保留する
internal sealed unsafe class ScriptFieldCodec {

    // 型登録と保存参照の変換を接続する
    internal ScriptFieldCodec(ScriptTypeRegistry registry) {

        this.registry = registry;
        jsonOptions = CreateJSONOptions();
        pendingReferences = new PendingScriptReferences(SetFieldFromElement);
    }

    // 保留値と旧Assemblyの型キャッシュを破棄する
    internal void ResetAssemblyState() {

        pendingReferences.Clear();
        pendingCallbacks.Clear();
        savedValues = new();
        jsonOptions = CreateJSONOptions();
    }

    // 破棄したScriptの保留値と型参照を手放す
    internal void ReleaseInstance(MonoBehaviour script) {

        pendingReferences.Remove(script);
        pendingCallbacks.Remove(script);
        savedValues.Remove(script);
    }

    // 保存Fieldを適用して参照解決を予約する
    internal void ApplySerializedFields(MonoBehaviour script, string? json) {

        ApplyFields(script, json, false);
    }

    // 再読込時だけprivateFieldも復元する
    internal void ApplyReloadFields(MonoBehaviour script, string? json) {

        ApplyFields(script, json, true);
    }

    // 取消済みの予約を除いて保留参照を適用する
    internal void FlushPendingReferenceFields(bool retryUnresolved = false) {

        if (flushing) {
            return;
        }
        flushing = true;
        try {
            pendingReferences.DiscardChangedReferences((script, field) =>
                savedValues.GetOrCreateValue(script).rejectedFields.Remove(field));
            if (retryUnresolved) {
                pendingReferences.RetryUnresolved();
            }
            pendingReferences.Flush();
            foreach (MonoBehaviour script in pendingCallbacks.ToArray()) {
                if (!pendingCallbacks.Remove(script)) {
                    continue;
                }
                ScriptSerializationCallbacks.Invoke(script, beforeSerialize: false);
            }
        }
        finally {
            flushing = false;
        }
    }

    // 保存Fieldを復元して未解決値を保持する
    internal void SetFieldFromElement(MonoBehaviour script, FieldInfo field, JsonElement value) {

        using var scope = referenceContext.Enter(script);
        try {
            ScriptReferenceGraph graph = GetReferenceGraph(script);
            object? deserialized = graph.ReadField(field.FieldType, JsonNode.Parse(value.GetRawText()),
                field.GetCustomAttribute<SerializeReferenceAttribute>() is not null);
            field.SetValue(script, deserialized);
            savedValues.GetOrCreateValue(script).rejectedFields.Remove(field);
        }
        catch (UnresolvedScriptReferenceException ex) {
            // 保存IDを残して明示的な再解決を待つ
            if (savedValues.GetOrCreateValue(script).rejectedFields.Add(field)) {
                NativeApplicationAPI.WriteLog(1, $"参照を保留しました: {script.GetType().FullName}.{field.Name}: {ex.Message}");
            }
            pendingReferences.AddUnresolved(script, field, value);
        }
        catch (Exception ex) {
            // 変換できない保存値を残し、実行値は変更しない
            savedValues.GetOrCreateValue(script).rejectedFields.Add(field);
            NativeApplicationAPI.WriteLog(1,
                $"Failed to apply field '{field.Name}' on '{script.GetType().FullName}': {ex.Message}");
        }
    }

    // 宣言型に対応する共有参照を保存する
    internal JsonNode? SerializeManagedReference(Type declaredType, object? value) {
        var graph = new ScriptReferenceGraph(registry.gameAssembly ?? declaredType.Assembly, jsonOptions);
        graph.BeginWrite();
        return graph.WriteField(declaredType, value, true);
    }

    // 編集値の変換成功後にFieldを更新する
    internal void ApplyFieldValue(MonoBehaviour script, FieldInfo field, string? valueJson) {

        using var scope = referenceContext.Enter(script);
        if (valueJson == null) {
            return;
        }
        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            throw new InvalidOperationException("Script type is not registered.");
        }
        string fieldID = entry.fieldMap.First(pair => pair.Value.Equals(field)).Key;
        if (entry.unsupportedFields.Contains(fieldID)) {
            throw new JsonException("Unsupported serialized field.");
        }
        // 共有参照のない値は対象Fieldだけを更新する
        if (field.GetCustomAttribute<SerializeReferenceAttribute>() is null &&
            !ScriptFieldTypeUtility.ContainsManagedReference(field.FieldType)) {
            ScriptReferenceGraph single = CreateReferenceGraph(script.GetType().Assembly);
            object? value = single.ReadField(field.FieldType, JsonNode.Parse(valueJson), false);
            field.SetValue(script, value);
            pendingReferences.Remove(script, field);
            savedValues.GetOrCreateValue(script).rejectedFields.Remove(field);
            return;
        }
        JsonObject source = JsonNode.Parse(BuildSavedStateFields(script))!.AsObject();
        var previousGraph = new ScriptReferenceGraph(script.GetType().Assembly, jsonOptions, source);
        source[fieldID] = JsonNode.Parse(valueJson);
        // 定義元の編集後も別Fieldが参照する個体を残す
        previousGraph.CompleteWrite(source);
        var graph = new ScriptReferenceGraph(script.GetType().Assembly, jsonOptions, source);
        var values = new Dictionary<FieldInfo, object?>();
        var rejected = new HashSet<FieldInfo>();
        foreach (KeyValuePair<string, FieldInfo> item in entry.fieldMap) {
            if (entry.unsupportedFields.Contains(item.Key) || !source.TryGetPropertyValue(item.Key, out JsonNode? node)) {
                continue;
            }
            try {
                values.Add(item.Value, graph.ReadField(item.Value.FieldType, node,
                    item.Value.GetCustomAttribute<SerializeReferenceAttribute>() is not null));
            }
            catch when (!item.Value.Equals(field)) {
                rejected.Add(item.Value);
            }
        }
        // 編集対象の変換成功後に共有参照を一組で公開する
        foreach (KeyValuePair<FieldInfo, object?> item in values) {
            item.Key.SetValue(script, item.Value);
            pendingReferences.Remove(script, item.Key);
        }
        ScriptSerializedValues saved = savedValues.GetOrCreateValue(script);
        saved.source = source;
        saved.graph = graph;
        saved.rejectedFields.Clear();
        saved.rejectedFields.UnionWith(rejected);
    }

    // Inspector用の取得とは分けて欠損Fieldも保存する
    internal string BuildSavedStateJSON(MonoBehaviour script) => BuildStateJSON(script, false);

    // 再読込専用の値を通常保存へ混ぜない
    internal string BuildReloadStateJSON(MonoBehaviour script) => BuildStateJSON(script, true);

    // 表示可能なFieldの実行値を取得する
    internal string BuildRuntimeStateJSON(MonoBehaviour script) {

        // 実行値の取得前に保留参照を適用する
        FlushPendingReferenceFields();

        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            return "{}";
        }
        var obj = new JsonObject();
        ScriptReferenceGraph graph = GetReferenceGraph(script);
        graph.BeginWrite();
        foreach (KeyValuePair<string, FieldInfo> kv in entry.runtimeFieldMap) {
            try {
                object? value = kv.Value.GetValue(script);
                obj[kv.Key] = graph.WriteField(kv.Value.FieldType, value,
                    kv.Value.GetCustomAttribute<SerializeReferenceAttribute>() is not null);
            }
            catch {
                // 取得できないFieldを省く
            }
        }
        return obj.ToJsonString();
    }

    // Assemblyの型情報で共有参照表を作成する
    internal ScriptReferenceGraph CreateReferenceGraph(Assembly assembly) => new(assembly, jsonOptions);

    // Fieldの初期値を保存形式へ変換する
    internal string SerializeFieldDefault(FieldInfo field, object defaults, ScriptReferenceGraph? sharedGraph = null) {

        // 初期値の変換失敗も型登録の失敗として返す
        object? value = field.GetValue(defaults);
        ScriptReferenceGraph graph = sharedGraph ??
            CreateReferenceGraph(registry.gameAssembly ?? field.DeclaringType!.Assembly);
        return graph.WriteField(field.FieldType, value, field.GetCustomAttribute<SerializeReferenceAttribute>() is not null)
            ?.ToJsonString() ?? "null";
    }

    // Fieldの参照解決待ちを保持する
    internal readonly PendingScriptReferences pendingReferences;

    private readonly ScriptTypeRegistry registry;
    private readonly ScriptReferenceContext referenceContext = new();
    private ConditionalWeakTable<MonoBehaviour, ScriptSerializedValues> savedValues = new();
    private readonly HashSet<MonoBehaviour> pendingCallbacks = new(ReferenceEqualityComparer.Instance);
    private bool flushing;
    private readonly HashSet<MonoBehaviour> serializing = new(ReferenceEqualityComparer.Instance);
    private JsonSerializerOptions jsonOptions;

    // 保存Fieldと参照型のJSON変換を構成する
    private JsonSerializerOptions CreateJSONOptions() {

        var options = new JsonSerializerOptions {
            IncludeFields = true,
            // 読取専用の計算値を保存対象から除く
            IgnoreReadOnlyProperties = true,
            TypeInfoResolver = CreateTypeInfoResolver()
        };
        options.Converters.Add(new UUIDJsonConverter());
        ScriptNumericConversion.Configure(options);
        options.Converters.Add(new EntityRefJsonConverter());
        options.Converters.Add(new GameObjectJsonConverter(referenceContext));
        options.Converters.Add(new AssetJsonConverterFactory());
        options.Converters.Add(new MonoBehaviourJsonConverterFactory(referenceContext));
        options.Converters.Add(new ComponentJsonConverterFactory(referenceContext));
        return options;
    }

    // 保存対象のprivateFieldもJSONへ含める
    private static IJsonTypeInfoResolver CreateTypeInfoResolver() {

        var resolver = new DefaultJsonTypeInfoResolver();
        resolver.Modifiers.Add(static typeInfo => {

            if (typeInfo.Kind != JsonTypeInfoKind.Object || !ScriptFieldTypeUtility.HasSerializableFlag(typeInfo.Type)) {
                return;
            }
            // Propertyを除外し、保存Fieldだけで入出力を構成する
            typeInfo.Properties.Clear();
            foreach (FieldInfo field in ScriptFieldTypeUtility.EnumerateNestedSerializedFields(typeInfo.Type)) {
                JsonPropertyInfo property = typeInfo.CreateJsonPropertyInfo(field.FieldType, field.Name);
                property.Get = field.GetValue;
                property.Set = field.SetValue;
                typeInfo.Properties.Add(property);
            }
        });
        return resolver;
    }

    // 保存対象を解決して対応するFieldへ適用する
    private void ApplyFields(MonoBehaviour script, string? json, bool includePrivate) {

        if (string.IsNullOrWhiteSpace(json)) {
            return;
        }
        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            throw new InvalidOperationException($"Script type is not registered: {script.GetType().FullName}");
        }

        using JsonDocument document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object) {
            throw new JsonException("Serialized script state must be an object.");
        }
        ScriptSerializedValues saved = savedValues.GetOrCreateValue(script);
        saved.source = JsonNode.Parse(json)!.AsObject();
        saved.rejectedFields.Clear();
        pendingReferences.Remove(script);
        saved.graph = new ScriptReferenceGraph(script.GetType().Assembly, jsonOptions, saved.source);
        pendingCallbacks.Add(script);
        Dictionary<string, FieldInfo>? privateFields = includePrivate ? BuildPrivateReloadFieldMap(script.GetType()) : null;
        foreach (JsonProperty prop in document.RootElement.EnumerateObject()) {
            if (!entry.fieldMap.TryGetValue(prop.Name, out FieldInfo? field) &&
                (privateFields == null || !privateFields.TryGetValue(prop.Name, out field))) {
                continue;
            }
            if (entry.fieldMap.ContainsKey(prop.Name) && entry.unsupportedFields.Contains(prop.Name)) {
                saved.rejectedFields.Add(field);
                continue;
            }
            if (entry.deferredFields.Contains(prop.Name) || ScriptFieldTypeUtility.IsDeferredReferenceType(field.FieldType, null)) {
                // JsonDocumentのdispose後も値を保持できるようCloneして積む
                pendingReferences.Add(script, field, prop.Value);
            } else {
                SetFieldFromElement(script, field, prop.Value);
            }
        }
    }

    // 再読込用のprivateFieldを索引化する
    private static Dictionary<string, FieldInfo> BuildPrivateReloadFieldMap(Type type) {

        var fields = new Dictionary<string, FieldInfo>(StringComparer.Ordinal);
        foreach (FieldInfo field in ScriptFieldTypeUtility.EnumerateNestedSerializedFields(type, true, typeof(MonoBehaviour))) {
            fields.TryAdd("$private:" + field.DeclaringType!.FullName + "/" + field.Name, field);
        }
        return fields;
    }

    // 保存元の参照表を遅延生成する
    private ScriptReferenceGraph GetReferenceGraph(MonoBehaviour script) {

        ScriptSerializedValues saved = savedValues.GetOrCreateValue(script);
        return saved.graph ??= new ScriptReferenceGraph(script.GetType().Assembly, jsonOptions, saved.source);
    }

    private string BuildStateJSON(MonoBehaviour script, bool includePrivate) {

        if (!serializing.Add(script)) {
            throw new InvalidOperationException("Recursive script serialization.");
        }
        try {
            FlushPendingReferenceFields();
            ScriptSerializationCallbacks.Invoke(script, beforeSerialize: true, includePrivate: includePrivate);
            return BuildSavedStateFields(script, includePrivate);
        }
        finally {
            serializing.Remove(script);
        }
    }

    // 欠損値を残して保存対象のFieldを構築する
    private string BuildSavedStateFields(MonoBehaviour script, bool includePrivate = false) {

        FlushPendingReferenceFields();
        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            throw new InvalidOperationException($"Script type is not registered: {script.GetType().FullName}");
        }
        ScriptSerializedValues saved = savedValues.GetOrCreateValue(script);
        var fields = new Dictionary<string, FieldInfo>(entry.fieldMap, StringComparer.Ordinal);
        if (includePrivate) {
            var known = new HashSet<FieldInfo>(fields.Values);
            foreach (FieldInfo field in ScriptFieldTypeUtility.EnumerateNestedSerializedFields(script.GetType(), true, typeof(MonoBehaviour))) {
                if (!known.Contains(field)) {
                    fields.Add("$private:" + field.DeclaringType!.FullName + "/" + field.Name, field);
                }
            }
        }
        JsonObject result = saved.source.DeepClone().AsObject();
        result.Remove("$managedReferences");
        if (!includePrivate) {
            // 再読込専用の値を通常保存へ混ぜない
            foreach (string key in result.Select(item => item.Key)
                .Where(key => key.StartsWith("$private:", StringComparison.Ordinal)).ToArray()) {
                result.Remove(key);
            }
        }
        foreach (KeyValuePair<string, FieldInfo> item in fields) {
            if (!entry.unsupportedFields.Contains(item.Key) && !saved.rejectedFields.Contains(item.Value)) {
                result.Remove(item.Key);
            }
        }
        ScriptReferenceGraph graph = GetReferenceGraph(script);
        graph.BeginWrite(result, includePrivate);
        foreach (KeyValuePair<string, FieldInfo> item in fields) {
            if (entry.unsupportedFields.Contains(item.Key) || saved.rejectedFields.Contains(item.Value)) {
                continue;
            }
            result[item.Key] = graph.WriteField(item.Value.FieldType, item.Value.GetValue(script),
                item.Value.GetCustomAttribute<SerializeReferenceAttribute>() is not null,
                saved.source[item.Key], preserve: true);
        }
        graph.CompleteWrite(result);
        return result.ToJsonString();
    }

}
