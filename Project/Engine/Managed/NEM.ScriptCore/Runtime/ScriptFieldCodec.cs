using System.Reflection;
using System.Runtime.CompilerServices;



using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization.Metadata;


namespace NEMEngine;

// 保存値と実行値を変換し参照適用を保留する
internal sealed unsafe class ScriptFieldCodec {

    private readonly ScriptTypeRegistry registry;
    private ConditionalWeakTable<MonoBehaviour, ScriptSerializedValues> savedValues = new();
    private readonly HashSet<MonoBehaviour> pendingCallbacks = new(ReferenceEqualityComparer.Instance);
    private bool flushing;
    private readonly HashSet<MonoBehaviour> serializing = new(ReferenceEqualityComparer.Instance);

    internal ScriptFieldCodec(ScriptTypeRegistry registry) {
        this.registry = registry;
        pendingReferences = new PendingScriptReferences(SetFieldFromElement);
    }

    private JsonSerializerOptions jsonOptions = CreateJsonOptions();

    // 保留値と旧Assemblyの型キャッシュを破棄する
    internal void ResetAssemblyState() {

        pendingReferences.Clear();
        pendingCallbacks.Clear();
        savedValues = new();
        jsonOptions = CreateJsonOptions();
    }

    // 破棄したScriptの保留値と型参照を手放す
    internal void ReleaseInstance(MonoBehaviour script) {
        pendingReferences.Remove(script);
        pendingCallbacks.Remove(script);
        savedValues.Remove(script);
    }

    private static JsonSerializerOptions CreateJsonOptions() {

        var options = new JsonSerializerOptions {
            IncludeFields = true,
            // MathTypesのlength/normalizedなどは保存値ではないのでJSON化しない
            IgnoreReadOnlyProperties = true,
            TypeInfoResolver = CreateTypeInfoResolver()
        };
        options.Converters.Add(new UUIDJsonConverter());
        ScriptNumericConversion.Configure(options);
        options.Converters.Add(new EntityRefJsonConverter());
        options.Converters.Add(new GameObjectJsonConverter());
        options.Converters.Add(new AssetJsonConverterFactory());
        options.Converters.Add(new MonoBehaviourJsonConverterFactory());
        options.Converters.Add(new ComponentJsonConverterFactory());
        return options;
    }

    // IncludeFieldsはpublicフィールドしか対象にしないため、
    // [Serializable]型のprivate [SerializeField]フィールドもJSONへ含めるresolverを作る
    private static IJsonTypeInfoResolver CreateTypeInfoResolver() {

        var resolver = new DefaultJsonTypeInfoResolver();
        resolver.Modifiers.Add(static typeInfo => {

            if (typeInfo.Kind != JsonTypeInfoKind.Object || !HasSerializableFlag(typeInfo.Type)) {
                return;
            }
            // Propertyを除外し、保存Fieldだけで入出力を構成する
            typeInfo.Properties.Clear();
            foreach (FieldInfo field in EnumerateNestedSerializedFields(typeInfo.Type)) {
                JsonPropertyInfo property = typeInfo.CreateJsonPropertyInfo(field.FieldType, field.Name);
                property.Get = field.GetValue;
                property.Set = field.SetValue;
                typeInfo.Properties.Add(property);
            }
        });
        return resolver;
    }

    internal readonly PendingScriptReferences pendingReferences;

    internal void ApplySerializedFields(MonoBehaviour script, string? json) {

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
        foreach (JsonProperty prop in document.RootElement.EnumerateObject()) {
            if (!entry.fieldMap.TryGetValue(prop.Name, out FieldInfo? field)) {
                continue;
            }
            if (entry.unsupportedFields.Contains(prop.Name)) {
                saved.rejectedFields.Add(field);
                continue;
            }
            if (entry.deferredFields.Contains(prop.Name)) {
                // JsonDocumentのdispose後も値を保持できるようCloneして積む
                pendingReferences.Add(script, field, prop.Value);
            } else {
                SetFieldFromElement(script, field, prop.Value);
            }
        }
    }

    internal void FlushPendingReferenceFields() {

        if (flushing) { return; }
        flushing = true;
        try {
            pendingReferences.Flush();
            foreach (MonoBehaviour script in pendingCallbacks.ToArray()) {
                if (!pendingCallbacks.Remove(script)) { continue; }
                ScriptSerializationCallbacks.Invoke(script, beforeSerialize: false);
            }
        }
        finally { flushing = false; }
    }

    internal void SetFieldFromElement(MonoBehaviour script, FieldInfo field, JsonElement value) {

        try {
            ScriptReferenceGraph graph = GetReferenceGraph(script);
            object? deserialized = graph.ReadField(field.FieldType, JsonNode.Parse(value.GetRawText()),
                field.GetCustomAttribute<SerializeReferenceAttribute>() is not null);
            field.SetValue(script, deserialized);
            savedValues.GetOrCreateValue(script).rejectedFields.Remove(field);
        }
        catch (Exception ex) {
            // 変換できない保存値を残し、実行値は変更しない
            savedValues.GetOrCreateValue(script).rejectedFields.Add(field);
            NativeApplicationAPI.WriteLog(1, $"Failed to apply field '{field.Name}' on '{script.GetType().FullName}': {ex.Message}");
        }
    }

    // 保存元の参照表を遅延生成する
    private ScriptReferenceGraph GetReferenceGraph(MonoBehaviour script) {
        ScriptSerializedValues saved = savedValues.GetOrCreateValue(script);
        return saved.graph ??= new ScriptReferenceGraph(script.GetType().Assembly, jsonOptions, saved.source);
    }

    internal JsonNode? SerializeManagedReference(Type declaredType, object? value) {
        var graph = new ScriptReferenceGraph(registry.gameAssembly ?? declaredType.Assembly, jsonOptions);
        graph.BeginWrite();
        return graph.WriteField(declaredType, value, true);
    }

    internal void ApplyFieldValue(MonoBehaviour script, FieldInfo field, string? valueJson) {

        if (valueJson == null) {
            return;
        }
        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            throw new InvalidOperationException("Script type is not registered.");
        }
        string fieldID = entry.fieldMap.First(pair => pair.Value.Equals(field)).Key;
        if (entry.unsupportedFields.Contains(fieldID)) { throw new JsonException("Unsupported serialized field."); }
        // 共有参照のない値は対象Fieldだけを更新する
        if (field.GetCustomAttribute<SerializeReferenceAttribute>() is null && !ContainsManagedReference(field.FieldType, new())) {
            ScriptReferenceGraph single = CreateReferenceGraph(script.GetType().Assembly);
            object? value = single.ReadField(field.FieldType, JsonNode.Parse(valueJson), false);
            field.SetValue(script, value);
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
            if (entry.unsupportedFields.Contains(item.Key) || !source.TryGetPropertyValue(item.Key, out JsonNode? node)) { continue; }
            try {
                values.Add(item.Value, graph.ReadField(item.Value.FieldType, node,
                    item.Value.GetCustomAttribute<SerializeReferenceAttribute>() is not null));
            }
            catch when (!item.Value.Equals(field)) { rejected.Add(item.Value); }
        }
        // 編集対象の変換成功後に共有参照を一組で公開する
        foreach (KeyValuePair<FieldInfo, object?> item in values) { item.Key.SetValue(script, item.Value); }
        ScriptSerializedValues saved = savedValues.GetOrCreateValue(script);
        saved.source = source;
        saved.graph = graph;
        saved.rejectedFields.Clear();
        saved.rejectedFields.UnionWith(rejected);
    }

    // Inspector用の取得とは分けて欠損Fieldも保存する
    internal string BuildSavedStateJson(MonoBehaviour script) => BuildStateJson(script, false);

    // Reload専用のprivate値を通常保存へ混ぜない
    internal string BuildReloadStateJson(MonoBehaviour script) => BuildStateJson(script, true);

    private string BuildStateJson(MonoBehaviour script, bool includePrivate) {
        if (!serializing.Add(script)) { throw new InvalidOperationException("Recursive script serialization."); }
        try {
            FlushPendingReferenceFields();
            ScriptSerializationCallbacks.Invoke(script, beforeSerialize: true, includePrivate: includePrivate);
            return BuildSavedStateFields(script, includePrivate);
        }
        finally { serializing.Remove(script); }
    }

    private string BuildSavedStateFields(MonoBehaviour script, bool includePrivate = false) {

        FlushPendingReferenceFields();
        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            throw new InvalidOperationException($"Script type is not registered: {script.GetType().FullName}");
        }
        ScriptSerializedValues saved = savedValues.GetOrCreateValue(script);
        var fields = new Dictionary<string, FieldInfo>(entry.fieldMap, StringComparer.Ordinal);
        if (includePrivate) {
            var known = new HashSet<FieldInfo>(fields.Values);
            foreach (FieldInfo field in EnumerateNestedSerializedFields(script.GetType(), true, typeof(MonoBehaviour))) {
                if (!known.Contains(field)) { fields.Add("$private:" + field.DeclaringType!.FullName + "/" + field.Name, field); }
            }
        }
        JsonObject result = saved.source.DeepClone().AsObject();
        result.Remove("$managedReferences");
        foreach (KeyValuePair<string, FieldInfo> item in fields) {
            if (!entry.unsupportedFields.Contains(item.Key) && !saved.rejectedFields.Contains(item.Value)) { result.Remove(item.Key); }
        }
        ScriptReferenceGraph graph = GetReferenceGraph(script);
        graph.BeginWrite(result, includePrivate);
        foreach (KeyValuePair<string, FieldInfo> item in fields) {
            if (entry.unsupportedFields.Contains(item.Key) || saved.rejectedFields.Contains(item.Value)) { continue; }
            result[item.Key] = graph.WriteField(item.Value.FieldType, item.Value.GetValue(script),
                item.Value.GetCustomAttribute<SerializeReferenceAttribute>() is not null, saved.source[item.Key], preserve: true);
        }
        graph.CompleteWrite(result);
        return result.ToJsonString();
    }

    internal byte[] GetRuntimeStateSnapshot(ScriptInstanceSlot slot, bool refresh) {

        if (refresh || slot.runtimeStateSnapshot == null) {
            slot.runtimeStateSnapshot = Encoding.UTF8.GetBytes(BuildRuntimeStateJson(slot.instance!));
        }
        return slot.runtimeStateSnapshot;
    }

    internal string BuildRuntimeStateJson(MonoBehaviour script) {

        // 未適用の参照フィールドが残っていると現在値が空に見えるため先に解決する
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
                // 取得できない field は省略する
            }
        }
        return obj.ToJsonString();
    }

    internal ScriptReferenceGraph CreateReferenceGraph(Assembly assembly) => new(assembly, jsonOptions);

    internal string SerializeFieldDefault(FieldInfo? field, object? defaults, ScriptReferenceGraph? sharedGraph = null) {

        if (field == null) {
            return "null";
        }
        // 初期値の変換失敗も型登録の失敗として返す
        object? value = defaults != null ? field.GetValue(defaults) : null;
        ScriptReferenceGraph graph = sharedGraph ?? CreateReferenceGraph(registry.gameAssembly ?? field.DeclaringType!.Assembly);
        return graph.WriteField(field.FieldType, value, field.GetCustomAttribute<SerializeReferenceAttribute>() is not null)
            ?.ToJsonString() ?? "null";
    }

#pragma warning disable SYSLIB0050
    internal static bool IsDeferredReferenceType(Type type, HashSet<Type>? visited) {

        if (type == typeof(GameObject) || typeof(Component).IsAssignableFrom(type)) {
            return true;
        }
        if (type.IsArray) {
            return IsDeferredReferenceType(type.GetElementType()!, visited);
        }
        if (type.IsGenericType) {
            Type def = type.GetGenericTypeDefinition();
            if (def == typeof(List<>) || def == typeof(Nullable<>)) {
                return IsDeferredReferenceType(type.GetGenericArguments()[0], visited);
            }
        }
        // [Serializable]ネスト型はメンバを辿る、自己参照型は訪問済みsetで打ち切る
        if (IsSerializableObjectType(type)) {
            visited ??= new HashSet<Type>();
            if (!visited.Add(type)) {
                return false;
            }
            foreach (FieldInfo field in EnumerateNestedSerializedFields(type)) {
                if (IsDeferredReferenceType(field.FieldType, visited)) {
                    return true;
                }
            }
        }
        return false;
    }

    internal static bool IsSerializableObjectType(Type type) {

        if (!HasSerializableFlag(type) || type.IsPrimitive || type.IsEnum || type == typeof(string) ||
            type.IsAbstract || type.IsGenericType) {
            return false;
        }
        if (!type.IsClass && !type.IsValueType) {
            return false;
        }
        // エンジンの参照型階層とBCL型は対象外
        return !typeof(Object).IsAssignableFrom(type) && type.Assembly != typeof(object).Assembly;
    }

    internal static bool HasSerializableFlag(Type type) {
        return (type.Attributes & TypeAttributes.Serializable) != 0;
    }

    internal static IEnumerable<FieldInfo> EnumerateNestedSerializedFields(Type type, bool includePrivate = false, Type? stopType = null) {

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        for (Type? t = type; t != null && t != typeof(object) && t != stopType; t = t.BaseType) {
            foreach (FieldInfo field in t.GetFields(flags)) {
                if (field.IsStatic || field.IsInitOnly || field.IsLiteral ||
                    field.GetCustomAttribute<NonSerializedAttribute>() != null) {
                    continue;
                }
                if (includePrivate || field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null ||
                    field.GetCustomAttribute<SerializeReferenceAttribute>() != null) {
                    if (IsSupportedFieldType(field.FieldType, field.GetCustomAttribute<SerializeReferenceAttribute>() != null)) {
                        yield return field;
                    }
                }
            }
        }
    }

    // 保存可能な型と一重の配列だけを対象にする
    internal static bool IsSupportedFieldType(Type type, bool managedReference, bool allowCollection = true) {
        if (type.IsArray) {
            return allowCollection && type.GetArrayRank() == 1 && IsSupportedFieldType(type.GetElementType()!, managedReference, false);
        }
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>)) {
            return allowCollection && IsSupportedFieldType(type.GetGenericArguments()[0], managedReference, false);
        }
        if (managedReference) {
            return !type.IsGenericType && type != typeof(string) && !typeof(Object).IsAssignableFrom(type) && (type.IsClass || type.IsInterface);
        }
        if (type.IsEnum) { return Enum.GetUnderlyingType(type) != typeof(long) && Enum.GetUnderlyingType(type) != typeof(ulong); }
        if (type == typeof(string) || type == typeof(bool) || type == typeof(byte) || type == typeof(sbyte) || type == typeof(short) ||
            type == typeof(ushort) || type == typeof(int) || type == typeof(uint) || type == typeof(long) || type == typeof(ulong) ||
            type == typeof(float) || type == typeof(double)) { return true; }
        return type == typeof(Vector2) || type == typeof(Vector3) || type == typeof(Vector4) || type == typeof(Quaternion) ||
            type == typeof(Color3) || type == typeof(Color4) || typeof(Object).IsAssignableFrom(type) || IsSerializableObjectType(type);
    }

    // ネストした参照属性も単一Field編集の対象判定に含める
    private static bool ContainsManagedReference(Type type, HashSet<Type> visited) {
        if (!visited.Add(type)) { return false; }
        if (type.IsArray) { return ContainsManagedReference(type.GetElementType()!, visited); }
        if (type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>)) {
            return ContainsManagedReference(type.GetGenericArguments()[0], visited);
        }
        return IsSerializableObjectType(type) && EnumerateNestedSerializedFields(type).Any(field =>
            field.GetCustomAttribute<SerializeReferenceAttribute>() is not null || ContainsManagedReference(field.FieldType, visited));
    }

    internal static bool CanReadRuntimeField(JsonObject field) {

        return !IsUnsupportedField(field) &&
            field["isHidden"]?.GetValue<bool>() != true;
    }

    internal static bool IsUnsupportedField(JsonObject field) {

        return string.Equals(field["kind"]?.GetValue<string>(), "Unsupported", StringComparison.OrdinalIgnoreCase);
    }
#pragma warning restore SYSLIB0050
}
