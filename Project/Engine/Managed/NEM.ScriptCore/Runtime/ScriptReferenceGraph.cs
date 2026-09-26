using System.Collections;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

// 保存元ごとに共有参照と循環参照を解決する
internal sealed class ScriptReferenceGraph {

    private readonly Assembly assembly;
    private readonly JsonSerializerOptions options;
    private readonly Dictionary<long, JsonObject> definitions = new();
    private readonly Dictionary<long, object> instances = new();
    private sealed record Identity(long value);
    private readonly ConditionalWeakTable<object, Identity> identities = new();
    private readonly ConditionalWeakTable<object, JsonObject> memberSources = new();
    private readonly HashSet<long> written = new();
    private readonly HashSet<object> inlinePath = new(ReferenceEqualityComparer.Instance);
    private long nextID = 1;
    private bool includePrivateFields;

    internal ScriptReferenceGraph(Assembly assembly, JsonSerializerOptions options, JsonNode? source = null) {
        this.assembly = assembly;
        this.options = options;
        IndexDefinitions(source);
    }

    // 後方にある参照先も読込前に登録する
    private void IndexDefinitions(JsonNode? node) {
        if (node is JsonArray array) {
            foreach (JsonNode? item in array) { IndexDefinitions(item); }
        } else if (node is JsonObject value) {
            if (value.ContainsKey("$id") && value.ContainsKey("type")) {
                long id = ReadID(value, "$id");
                if (!definitions.TryAdd(id, value)) {
                    throw new JsonException($"Duplicate managed reference ID: {id}");
                }
                nextID = System.Math.Max(nextID, checked(id + 1));
            }
            foreach (KeyValuePair<string, JsonNode?> item in value) { IndexDefinitions(item.Value); }
        }
    }

    // 失敗したFieldの途中生成を公開しない
    internal object? ReadField(Type type, JsonNode? value, bool managedReference) {
        var previous = new HashSet<long>(instances.Keys);
        try { return ReadValue(type, value, managedReference, 0); }
        catch {
            foreach (long id in instances.Keys.Where(id => !previous.Contains(id)).ToArray()) {
                identities.Remove(instances[id]);
                instances.Remove(id);
            }
            throw;
        }
    }

    private object? ReadValue(Type type, JsonNode? value, bool managedReference, int depth) {
        CheckDepth(depth);
        if (value is null) { return null; }
        if (ElementType(type) is Type element) {
            if (value is not JsonArray items) { throw new JsonException("Expected a serialized array."); }
            IList result = type.IsArray ? Array.CreateInstance(element, items.Count) : (IList)Activator.CreateInstance(type)!;
            for (int i = 0; i < items.Count; ++i) {
                object? item = ReadValue(element, items[i], managedReference, depth + 1);
                if (type.IsArray) { result[i] = item; } else { result.Add(item); }
            }
            return result;
        }
        if (managedReference) { return ReadReference(type, value, depth); }
        if (ScriptNumericConversion.TryRead(type, value, out object? numeric)) { return numeric; }
        if (ScriptFieldCodec.IsSerializableObjectType(type)) {
            object instance = CreateInstance(type);
            ReadMembers(instance, type, value, depth);
            return instance;
        }
        return value.Deserialize(type, options);
    }

    private object? ReadReference(Type declaredType, JsonNode value, int depth) {
        if (value is not JsonObject record) { throw new JsonException("Expected a managed reference record."); }
        long id = 0;
        if (record.ContainsKey("$ref")) {
            id = ReadID(record, "$ref");
            if (instances.TryGetValue(id, out object? existing)) {
                ValidateType(declaredType, existing.GetType());
                return existing;
            }
            if (!definitions.TryGetValue(id, out record!)) { throw new JsonException($"Missing managed reference ID: {id}"); }
        } else if (record.ContainsKey("$id")) {
            id = ReadID(record, "$id");
        }
        string typeName = record["type"]?.GetValue<string>() ?? throw new JsonException("Missing managed reference type.");
        if (typeName.Length == 0) { return null; }
        Type actual = ScriptGeneratedMetadata.ResolveType(assembly, typeName) ?? throw new JsonException($"Missing managed reference type: {typeName}");
        ValidateType(declaredType, actual);
        if (id != 0 && instances.TryGetValue(id, out object? shared)) { return shared; }

        // メンバ読込前に個体を登録し、自己参照を解決する
        object instance = CreateInstance(actual);
        if (id != 0) {
            instances.Add(id, instance);
            identities.Add(instance, new Identity(id));
        }
        ReadMembers(instance, actual, record["value"] ?? new JsonObject(), depth);
        return instance;
    }

    // 引数なしコンストラクタを持たない保存型も復元する
    private static object CreateInstance(Type type) {
        if (type.IsValueType || type.GetConstructor(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic,
            binder: null, Type.EmptyTypes, modifiers: null) != null) {
            return Activator.CreateInstance(type, nonPublic: true)!;
        }
        return RuntimeHelpers.GetUninitializedObject(type);
    }

    private void ReadMembers(object instance, Type type, JsonNode value, int depth) {
        if (value is not JsonObject members) { throw new JsonException("Expected serialized fields."); }
        FieldInfo[] fields = ScriptFieldCodec.EnumerateNestedSerializedFields(type).ToArray();
        var names = new Dictionary<string, FieldInfo>(StringComparer.Ordinal);

        // 現在名と旧名の重複を適用前に検証する
        foreach (FieldInfo field in fields) {
            RegisterName(field.Name, field);
            foreach (FormerlySerializedAsAttribute former in field.GetCustomAttributes<FormerlySerializedAsAttribute>()) {
                RegisterName(former.OldName, field);
            }
        }
        foreach (FieldInfo field in fields) {
            string? key = members.ContainsKey(field.Name) ? field.Name : null;
            if (key is null) {
                foreach (FormerlySerializedAsAttribute former in field.GetCustomAttributes<FormerlySerializedAsAttribute>()) {
                    if (!members.ContainsKey(former.OldName)) { continue; }
                    if (key is not null && key != former.OldName) {
                        throw new JsonException($"Ambiguous former field names: {type.FullName}.{field.Name}");
                    }
                    key = former.OldName;
                }
            }
            if (key is not null && members.TryGetPropertyValue(key, out JsonNode? item)) {
                field.SetValue(instance, ReadValue(field.FieldType, item, IsManaged(field), depth + 1));
            }
        }
        if (!type.IsValueType) { memberSources.Add(instance, members); }

        void RegisterName(string name, FieldInfo field) {
            if (string.IsNullOrWhiteSpace(name) || names.TryGetValue(name, out FieldInfo? owner) && owner != field) {
                throw new JsonException($"Duplicate serialized field name: {type.FullName}.{name}");
            }
            names[name] = field;
        }
    }

    internal void BeginWrite(JsonNode? preserved = null, bool includePrivate = false) {
        includePrivateFields = includePrivate;
        written.Clear();
        inlinePath.Clear();
        VisitRecords(preserved, record => {
            if (record.ContainsKey("$id")) { written.Add(ReadID(record, "$id")); }
        });
    }

    // 欠損Fieldだけが参照する旧定義も保持する
    internal void CompleteWrite(JsonObject result) {
        result.Remove("$managedReferences");
        var defined = new HashSet<long>();
        var referenced = new Queue<long>();
        void Collect(JsonObject record) {
            if (record.ContainsKey("$id")) { defined.Add(ReadID(record, "$id")); }
            if (record.ContainsKey("$ref")) { referenced.Enqueue(ReadID(record, "$ref")); }
        }
        VisitRecords(result, Collect);
        var retained = new JsonArray();
        while (referenced.TryDequeue(out long id)) {
            if (defined.Contains(id)) { continue; }
            if (!definitions.TryGetValue(id, out JsonObject? original)) {
                throw new JsonException($"Missing retained managed reference: {id}");
            }
            JsonNode copy = CopyDefinition(original, defined);
            VisitRecords(copy, Collect);
            retained.Add(copy);
        }
        if (retained.Count != 0) { result["$managedReferences"] = retained; }
    }

    private static JsonNode CopyDefinition(JsonNode node, HashSet<long> defined) {
        if (node is JsonObject record) {
            if (record.ContainsKey("$id") && record.ContainsKey("type") && !defined.Add(ReadID(record, "$id"))) {
                return new JsonObject { ["$ref"] = ReadID(record, "$id") };
            }
            var copy = new JsonObject();
            foreach (KeyValuePair<string, JsonNode?> item in record) {
                copy.Add(item.Key, item.Value is null ? null : CopyDefinition(item.Value, defined));
            }
            return copy;
        }
        if (node is JsonArray array) {
            var copy = new JsonArray();
            foreach (JsonNode? item in array) { copy.Add(item is null ? null : CopyDefinition(item, defined)); }
            return copy;
        }
        return node.DeepClone();
    }

    private static void VisitRecords(JsonNode? node, Action<JsonObject> visit) {
        if (node is JsonObject record) {
            if (record.ContainsKey("$ref") || record.ContainsKey("$id") && record.ContainsKey("type")) { visit(record); }
            foreach (KeyValuePair<string, JsonNode?> item in record) { VisitRecords(item.Value, visit); }
        } else if (node is JsonArray array) {
            foreach (JsonNode? item in array) { VisitRecords(item, visit); }
        }
    }

    internal JsonNode? WriteField(Type type, object? value, bool managedReference, JsonNode? source = null, bool preserve = false) {
        var previous = new HashSet<long>(written);
        try { return WriteValue(type, value, managedReference, 0, source, preserve); }
        catch {
            written.IntersectWith(previous);
            throw;
        }
    }

    private JsonNode? WriteValue(Type type, object? value, bool managedReference, int depth, JsonNode? source, bool preserve) {
        CheckDepth(depth);
        if (ElementType(type) is Type element) {
            if (value is null) { return null; }
            var array = new JsonArray();
            int index = 0;
            foreach (object? item in (IEnumerable)value) {
                JsonNode? previous = source is JsonArray old && index < old.Count ? old[index] : null;
                array.Add(WriteValue(element, item, managedReference, depth + 1, previous, preserve));
                ++index;
            }
            return array;
        }
        if (managedReference) {
            if (value is null) { return new JsonObject { ["type"] = "", ["value"] = new JsonObject() }; }
            Type actual = value.GetType();
            ValidateType(type, actual);
            if (!identities.TryGetValue(value, out Identity? identity)) {
                identity = new Identity(nextID);
                nextID = checked(nextID + 1);
                identities.Add(value, identity);
            }
            long id = identity.value;
            if (!written.Add(id)) { return new JsonObject { ["$ref"] = id }; }
            return new JsonObject { ["$id"] = id, ["type"] = actual.FullName?.Replace('+', '.'),
                ["value"] = WriteMembers(actual, value, depth, null, preserve) };
        }
        if (value is not null && ScriptFieldCodec.IsSerializableObjectType(type)) {
            if (!inlinePath.Add(value)) { throw new JsonException("Inline fields cannot contain a cycle. Use SerializeReference."); }
            try { return WriteMembers(type, value, depth, source, preserve); }
            finally { inlinePath.Remove(value); }
        }
        return JsonSerializer.SerializeToNode(value, type, options);
    }

    private JsonObject WriteMembers(Type type, object value, int depth, JsonNode? source, bool preserve) {
        if (!type.IsValueType) {
            if (memberSources.TryGetValue(value, out JsonObject? original)) { source = original; }
        }
        var members = new JsonObject();
        FieldInfo[] fields = ScriptFieldCodec.EnumerateNestedSerializedFields(type, includePrivateFields).ToArray();

        // 未知のメンバを残し、既知の旧名は現在名へ置き換える
        if (preserve && source is JsonObject previous) {
            var known = new HashSet<string>(fields.Select(field => field.Name), StringComparer.Ordinal);
            foreach (FieldInfo field in fields) {
                foreach (FormerlySerializedAsAttribute former in field.GetCustomAttributes<FormerlySerializedAsAttribute>()) {
                    known.Add(former.OldName);
                }
            }
            foreach (KeyValuePair<string, JsonNode?> item in previous) {
                if (!known.Contains(item.Key)) { members.Add(item.Key, CopyRetainedValue(item.Value)); }
            }
        }
        foreach (FieldInfo field in fields) {
            JsonNode? old = (source as JsonObject)?[field.Name];
            if (source is JsonObject oldMembers && !oldMembers.ContainsKey(field.Name)) {
                foreach (FormerlySerializedAsAttribute former in field.GetCustomAttributes<FormerlySerializedAsAttribute>()) {
                    if (oldMembers.TryGetPropertyValue(former.OldName, out old)) { break; }
                }
            }
            members.Add(field.Name, WriteValue(field.FieldType, field.GetValue(value), IsManaged(field), depth + 1, old, preserve));
        }
        return members;
    }

    // 旧定義を参照へ変え、保存完了時に必要な定義だけ補う
    private static JsonNode? CopyRetainedValue(JsonNode? source) {
        if (source is JsonObject record) {
            if (record.ContainsKey("$id") && record.ContainsKey("type")) {
                return new JsonObject { ["$ref"] = ReadID(record, "$id") };
            }
            var copy = new JsonObject();
            foreach (KeyValuePair<string, JsonNode?> item in record) { copy.Add(item.Key, CopyRetainedValue(item.Value)); }
            return copy;
        }
        if (source is JsonArray array) {
            var copy = new JsonArray();
            foreach (JsonNode? item in array) { copy.Add(CopyRetainedValue(item)); }
            return copy;
        }
        return source?.DeepClone();
    }

    private static Type? ElementType(Type type) => type.IsArray && type.GetArrayRank() == 1 ? type.GetElementType() :
        type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>) ? type.GetGenericArguments()[0] : null;

    private static bool IsManaged(FieldInfo field) => field.GetCustomAttribute<SerializeReferenceAttribute>() is not null;

    private static long ReadID(JsonObject value, string key) {
        long id = value[key]?.GetValue<long>() ?? 0;
        return id > 0 ? id : throw new JsonException($"Invalid managed reference ID: {id}");
    }

    private static void ValidateType(Type declaredType, Type actual) {
        if (!actual.IsClass || actual.IsAbstract || actual.IsGenericType || actual.ContainsGenericParameters || actual == typeof(string) ||
            typeof(Object).IsAssignableFrom(actual) || !ScriptFieldCodec.HasSerializableFlag(actual) || !declaredType.IsAssignableFrom(actual)) {
            throw new JsonException($"Invalid managed reference type: {actual.FullName}");
        }
    }

    private static void CheckDepth(int depth) {
        if (depth > 64) { throw new JsonException("Serialized field nesting exceeds 64 levels."); }
    }
}
