using System.Reflection;



using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization.Metadata;


namespace NEMEngine;

// 保存値と実行値を変換し参照適用を保留する
internal sealed unsafe class ScriptFieldCodec {

    private readonly ScriptTypeRegistry registry;

    internal ScriptFieldCodec(ScriptTypeRegistry registry) {
        this.registry = registry;
        pendingReferences = new PendingScriptReferences(SetFieldFromElement);
    }

    private readonly JsonSerializerOptions jsonOptions = CreateJsonOptions();

    private static JsonSerializerOptions CreateJsonOptions() {

        var options = new JsonSerializerOptions {
            IncludeFields = true,
            // MathTypesのlength/normalizedなどは保存値ではないのでJSON化しない
            IgnoreReadOnlyProperties = true,
            TypeInfoResolver = CreateTypeInfoResolver()
        };
        options.Converters.Add(new UUIDJsonConverter());
        options.Converters.Add(new EntityRefJsonConverter());
        options.Converters.Add(new EntityJsonConverter());
        options.Converters.Add(new AssetJsonConverterFactory());
        options.Converters.Add(new ScriptBehaviourJsonConverterFactory());
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
            // 基底クラスのprivateフィールドはGetFieldsで列挙されないため継承チェーンを辿る
            const BindingFlags flags = BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
            for (Type? t = typeInfo.Type; t != null && t != typeof(object); t = t.BaseType) {
                foreach (FieldInfo fieldInfo in t.GetFields(flags)) {
                    if (fieldInfo.GetCustomAttribute<SerializeFieldAttribute>() == null) {
                        continue;
                    }
                    JsonPropertyInfo property = typeInfo.CreateJsonPropertyInfo(fieldInfo.FieldType, fieldInfo.Name);
                    property.Get = fieldInfo.GetValue;
                    property.Set = fieldInfo.SetValue;
                    typeInfo.Properties.Add(property);
                }
            }
        });
        return resolver;
    }

    internal readonly PendingScriptReferences pendingReferences;

    internal void ApplySerializedFields(ScriptBehaviour script, string? json) {

        if (string.IsNullOrWhiteSpace(json)) {
            return;
        }
        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            return;
        }

        using JsonDocument document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object) {
            return;
        }
        foreach (JsonProperty prop in document.RootElement.EnumerateObject()) {
            if (!entry.fieldMap.TryGetValue(prop.Name, out FieldInfo? field)) {
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

        pendingReferences.Flush();
    }

    internal void SetFieldFromElement(ScriptBehaviour script, FieldInfo field, JsonElement value) {

        try {
            // [SerializeReference]は保存型を候補検証してからインスタンス化する
            object? deserialized = field.GetCustomAttribute<SerializeReferenceAttribute>() != null
                ? DeserializeManagedReference(field.FieldType, value)
                : value.Deserialize(field.FieldType, jsonOptions);
            field.SetValue(script, deserialized);
        }
        catch (Exception ex) {
            // 型不一致などはその field だけ skip し、他 field と instance を壊さない
            NativeApplicationAPI.WriteLog(1, $"Failed to apply field '{field.Name}' on '{script.GetType().FullName}': {ex.Message}");
        }
    }

    internal object? DeserializeManagedReference(Type declaredType, JsonElement value) {

        if (declaredType.IsArray) {
            Type element = declaredType.GetElementType()!;
            if (value.ValueKind != JsonValueKind.Array) {
                return null;
            }
            var array = Array.CreateInstance(element, value.GetArrayLength());
            int index = 0;
            foreach (JsonElement item in value.EnumerateArray()) {
                array.SetValue(DeserializeManagedReferenceValue(element, item), index++);
            }
            return array;
        }
        if (declaredType.IsGenericType && declaredType.GetGenericTypeDefinition() == typeof(List<>)) {
            Type element = declaredType.GetGenericArguments()[0];
            var list = (System.Collections.IList)Activator.CreateInstance(declaredType)!;
            if (value.ValueKind == JsonValueKind.Array) {
                foreach (JsonElement item in value.EnumerateArray()) {
                    list.Add(DeserializeManagedReferenceValue(element, item));
                }
            }
            return list;
        }
        return DeserializeManagedReferenceValue(declaredType, value);
    }

    internal object? DeserializeManagedReferenceValue(Type baseType, JsonElement value) {

        if (value.ValueKind != JsonValueKind.Object ||
            !value.TryGetProperty("type", out JsonElement typeElement) || typeElement.ValueKind != JsonValueKind.String) {
            return null;
        }
        string typeName = typeElement.GetString() ?? string.Empty;
        if (string.IsNullOrEmpty(typeName) || registry.gameAssembly == null) {
            return null;
        }
        Type? resolved = registry.gameAssembly.GetType(typeName, throwOnError: false);
        if (resolved == null || resolved.IsAbstract || !HasSerializableFlag(resolved) || !baseType.IsAssignableFrom(resolved)) {
            return null;
        }
        return value.TryGetProperty("value", out JsonElement body) && body.ValueKind == JsonValueKind.Object
            ? body.Deserialize(resolved, jsonOptions)
            : Activator.CreateInstance(resolved);
    }

    internal JsonNode? SerializeManagedReference(Type declaredType, object? value) {

        if (declaredType.IsArray ||
            (declaredType.IsGenericType && declaredType.GetGenericTypeDefinition() == typeof(List<>))) {

            var array = new JsonArray();
            if (value is System.Collections.IEnumerable items) {
                foreach (object? item in items) {
                    array.Add(SerializeManagedReferenceValue(item));
                }
            }
            return array;
        }
        return SerializeManagedReferenceValue(value);
    }

    internal JsonNode SerializeManagedReferenceValue(object? value) {

        if (value == null) {
            return new JsonObject { ["type"] = "", ["value"] = new JsonObject() };
        }
        Type actual = value.GetType();
        return new JsonObject {
            ["type"] = actual.FullName ?? string.Empty,
            ["value"] = JsonSerializer.SerializeToNode(value, actual, jsonOptions),
        };
    }

    internal void ApplyFieldValue(ScriptBehaviour script, FieldInfo field, string? valueJson) {

        if (valueJson == null) {
            return;
        }
        using JsonDocument document = JsonDocument.Parse(valueJson);
        SetFieldFromElement(script, field, document.RootElement);
    }

    internal byte[] GetRuntimeStateSnapshot(ScriptInstanceSlot slot, bool refresh) {

        if (refresh || slot.runtimeStateSnapshot == null) {
            slot.runtimeStateSnapshot = Encoding.UTF8.GetBytes(BuildRuntimeStateJson(slot.instance!));
        }
        return slot.runtimeStateSnapshot;
    }

    internal string BuildRuntimeStateJson(ScriptBehaviour script) {

        // 未適用の参照フィールドが残っていると現在値が空に見えるため先に解決する
        FlushPendingReferenceFields();

        if (!registry.typeToEntry.TryGetValue(script.GetType(), out ScriptTypeEntry? entry)) {
            return "{}";
        }
        var obj = new JsonObject();
        foreach (KeyValuePair<string, FieldInfo> kv in entry.runtimeFieldMap) {
            try {
                object? value = kv.Value.GetValue(script);
                obj[kv.Key] = kv.Value.GetCustomAttribute<SerializeReferenceAttribute>() != null
                    ? SerializeManagedReference(kv.Value.FieldType, value)
                    : JsonSerializer.SerializeToNode(value, kv.Value.FieldType, jsonOptions);
            }
            catch {
                // 取得できない field は省略する
            }
        }
        return obj.ToJsonString();
    }

    internal string SerializeFieldDefault(FieldInfo? field, object? defaults) {

        if (field == null) {
            return "null";
        }
        try {
            object? value = defaults != null ? field.GetValue(defaults) : null;
            if (field.GetCustomAttribute<SerializeReferenceAttribute>() != null) {
                return SerializeManagedReference(field.FieldType, value)?.ToJsonString() ?? "null";
            }
            return JsonSerializer.Serialize(value, field.FieldType, jsonOptions);
        }
        catch {
            return "null";
        }
    }

#pragma warning disable SYSLIB0050
    internal static bool IsDeferredReferenceType(Type type, HashSet<Type>? visited) {

        if (type == typeof(Entity) || typeof(Component).IsAssignableFrom(type)) {
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

    internal static IEnumerable<FieldInfo> EnumerateNestedSerializedFields(Type type) {

        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        for (Type? t = type; t != null && t != typeof(object); t = t.BaseType) {
            foreach (FieldInfo field in t.GetFields(flags)) {
                if (field.IsStatic || field.IsInitOnly || field.IsLiteral) {
                    continue;
                }
                if (field.IsPublic || field.GetCustomAttribute<SerializeFieldAttribute>() != null) {
                    yield return field;
                }
            }
        }
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
