using System.Linq.Expressions;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

// 型と保存FieldをAssembly単位で解決して再生中の個体を所有する
internal sealed class TimelineClipRegistry {
    private sealed class SuppressedPreviewClip : TimelineClip {
        public override void Evaluate(TimelineContext context) { }
    }
    private sealed record FieldEntry(string ID, FieldInfo Field, Action<TimelineClip, object?> Set,
        Func<GameObject, object?>? ResolveBinding, string[] FormerNames);
    private sealed record TypeEntry(string ID, string Name, string DisplayName, bool SupportsPreview,
        Func<TimelineClip> Create, FieldEntry[] Fields);
    private sealed class InstanceEntry(TimelineClip clip, TimelineContext context, TypeEntry type, NativeEntity owner) {
        internal readonly TimelineClip clip = clip;
        internal readonly TimelineContext context = context;
        internal readonly bool supportsPreview = type.SupportsPreview;
        internal readonly TypeEntry type = type;
        internal readonly NativeEntity owner = owner;
        internal bool entered;
        internal bool faulted;
        internal bool bindingsPrepared;
        internal readonly List<(FieldEntry field, string slot)> bindingFields = new();
    }

    private readonly Dictionary<string, TypeEntry> types = new(StringComparer.Ordinal);
    private readonly Dictionary<ulong, InstanceEntry> instances = new();
    private Assembly? assembly;
    private ulong nextHandle = 1;
    private byte[] schema = "[]"u8.ToArray();

    // 個体と型の参照を切って再読込先のAssemblyを登録する
    internal void Load(Assembly loadedAssembly) {
        Clear();
        assembly = loadedAssembly;
        var schemas = new JsonArray();
        foreach (Type type in loadedAssembly.GetTypes().OrderBy(type => type.FullName, StringComparer.Ordinal)) {
            if (type.IsAbstract || type.ContainsGenericParameters || !typeof(TimelineClip).IsAssignableFrom(type)) continue;
            try {
                TypeEntry entry = Describe(type);
                if (types.ContainsKey(entry.ID)) throw new InvalidOperationException($"Timelineの型IDが重複しています: {entry.ID}");
                JsonObject description = CreateSchema(entry);
                types.Add(entry.ID, entry);
                if (entry.ID != entry.Name) types.TryAdd(entry.Name, entry);
                schemas.Add(description);
            } catch (Exception ex) {
                NativeApplicationAPI.WriteLog(2, $"Timelineクリップを登録できません: {type.FullName}: {ex.Message}");
            }
        }
        schema = System.Text.Encoding.UTF8.GetBytes(schemas.ToJsonString());
    }

    // 生成時と編集確定時だけ保存データを解釈する
    internal ManagedStatus Create(NativeEntity owner, string typeName, string configuration, out ulong handle) {
        handle = 0;
        if (assembly == null || !types.TryGetValue(typeName, out TypeEntry? entry)) return ManagedStatus.InvalidArgument;
        try {
            JsonObject data = JsonNode.Parse(configuration)?.AsObject() ?? new JsonObject();
            bool suppressed = !entry.SupportsPreview &&
                data["evaluationMode"]?.GetValue<int>() == (int)TimelineEvaluationMode.Preview;
            TimelineClip clip = suppressed ? new SuppressedPreviewClip() : entry.Create();
            var instance = new InstanceEntry(clip, new TimelineContext(owner, data), entry, owner);
            if (!suppressed) ApplyFields(instance, ReadFields(entry, owner, data, assembly));
            handle = nextHandle++;
            if (handle == 0) handle = nextHandle++;
            instances.Add(handle, instance);
            return ManagedStatus.Ok;
        } catch (Exception ex) {
            ScriptInvocationDiagnostics.ReportRuntimeServiceException($"Timeline.Create:{typeName}", ex);
            return ManagedStatus.ScriptException;
        }
    }

    // 再生個体と開始処理を保持して確定した編集設定だけを更新する
    internal ManagedStatus Reconfigure(ulong handle, string typeName, string configuration) {
        if (!instances.TryGetValue(handle, out InstanceEntry? entry)) return ManagedStatus.InvalidInstanceHandle;
        if (entry.faulted) return ManagedStatus.ScriptException;
        if (assembly == null || !types.TryGetValue(typeName, out TypeEntry? type) || type.ID != entry.type.ID)
            return ManagedStatus.InvalidArgument;
        try {
            JsonObject data = JsonNode.Parse(configuration)?.AsObject() ?? new JsonObject();
            var context = new TimelineContext(entry.owner, data);
            List<(FieldEntry field, object? value, string? slot)> values = entry.clip is SuppressedPreviewClip ? [] :
                ReadFields(type, entry.owner, data, assembly);
            entry.context.ApplyConfiguration(context);
            ApplyFields(entry, values);
            return ManagedStatus.Ok;
        } catch (Exception ex) {
            ScriptInvocationDiagnostics.ReportRuntimeServiceException($"Timeline.Configure:{typeName}", ex);
            return ManagedStatus.InvalidArgument;
        }
    }

    // 更新中は既に生成した個体の仮想関数だけを呼び出す
    internal ManagedStatus Evaluate(ulong handle, in NativeTimelineFrame frame) {
        if (!instances.TryGetValue(handle, out InstanceEntry? entry)) return ManagedStatus.InvalidInstanceHandle;
        if (entry.faulted) return ManagedStatus.ScriptException;
        if (frame.evaluationMode == (int)TimelineEvaluationMode.Preview && !entry.supportsPreview) return ManagedStatus.Ok;
        if (frame.phase is < 0 or > 2 || frame.evaluationMode is < 0 or > 2 || frame.exitReason is < 0 or > 7 || frame.isScrubbing is < 0 or > 1 ||
            !double.IsFinite(frame.time) || !double.IsFinite(frame.localTime) ||
            !double.IsFinite(frame.deltaTime) || !double.IsFinite(frame.duration) || !float.IsFinite(frame.weight))
            return ManagedStatus.InvalidArgument;
        TimelineContext context = entry.context;
        context.Time = frame.time;
        context.LocalTime = frame.localTime;
        context.DeltaTime = frame.deltaTime;
        context.Duration = frame.duration;
        context.Weight = frame.weight;
        context.Mode = (TimelineEvaluationMode)frame.evaluationMode;
        context.IsScrubbing = frame.isScrubbing != 0;
        try {
            if (!entry.bindingsPrepared) {
                entry.bindingsPrepared = true;
                foreach ((FieldEntry field, string slot) in entry.bindingFields) {
                    GameObject? target = context.GetBinding(slot);
                    field.Set(entry.clip, target == null ? null : field.ResolveBinding!(target));
                }
            }
            if (frame.phase == 0) {
                if (!entry.entered) { entry.entered = true; entry.clip.OnEnter(context); }
            } else if (frame.phase == 1) {
                if (!entry.entered) { entry.entered = true; entry.clip.OnEnter(context); }
                entry.clip.Evaluate(context);
            } else {
                Exit(entry, (TimelineExitReason)frame.exitReason);
            }
            return ManagedStatus.Ok;
        } catch (Exception ex) {
            entry.faulted = true;
            ScriptInvocationDiagnostics.ReportRuntimeServiceException($"Timeline:{entry.clip.GetType().FullName}", ex);
            try { Exit(entry, TimelineExitReason.Faulted); } catch { }
            return ManagedStatus.ScriptException;
        }
    }

    internal ManagedStatus Release(ulong handle, TimelineExitReason reason) {
        if (!instances.Remove(handle, out InstanceEntry? entry)) return ManagedStatus.InvalidInstanceHandle;
        try { Exit(entry, reason); return ManagedStatus.Ok; }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.ReportRuntimeServiceException($"Timeline.Exit:{entry.clip.GetType().FullName}", ex);
            return ManagedStatus.ScriptException;
        }
    }

    internal unsafe ManagedStatus UpdateContext(ulong handle, ReadOnlySpan<NativeTimelineBindingUpdate> bindings,
        ReadOnlySpan<NativeTimelineParameterUpdate> parameters) {
        if (!instances.TryGetValue(handle, out InstanceEntry? entry)) return ManagedStatus.InvalidInstanceHandle;
        try {
            if (entry.context.Update(bindings, parameters)) entry.bindingsPrepared = false;
            return ManagedStatus.Ok;
        } catch (Exception ex) {
            ScriptInvocationDiagnostics.ReportRuntimeServiceException("Timeline.Context", ex);
            return ManagedStatus.InvalidArgument;
        }
    }

    internal byte[] Schema => schema;

    // 旧Assemblyを参照する個体と変換処理を残さない
    internal void Clear() {
        foreach (ulong handle in instances.Keys.ToArray()) Release(handle, TimelineExitReason.Reload);
        instances.Clear();
        types.Clear();
        assembly = null;
        schema = "[]"u8.ToArray();
    }

    private static void Exit(InstanceEntry entry, TimelineExitReason reason) {
        if (!entry.entered) return;
        entry.entered = false;
        entry.clip.OnExit(entry.context, reason);
    }

    private static bool ReadField(JsonObject values, FieldEntry field, out JsonNode? value) {
        if (values.TryGetPropertyValue(field.ID, out value) || values.TryGetPropertyValue(field.Field.Name, out value)) return true;
        foreach (string name in field.FormerNames) if (values.TryGetPropertyValue(name, out value)) return true;
        value = null;
        return false;
    }

    // 型変換を完了してから個体のFieldへまとめて適用する
    private static List<(FieldEntry field, object? value, string? slot)> ReadFields(TypeEntry type, NativeEntity owner,
        JsonObject data, Assembly assembly) {
        JsonObject fields = data["fields"] as JsonObject ?? data;
        var options = ScriptFieldCodec.CreateJSONOptions(new ScriptReferenceContext { owner = owner });
        var graph = new ScriptReferenceGraph(assembly, options, fields);
        var values = new List<(FieldEntry field, object? value, string? slot)>();
        foreach (FieldEntry field in type.Fields) {
            if (!ReadField(fields, field, out JsonNode? value)) continue;
            if (value is JsonObject reference && reference["binding"] is JsonValue binding && field.ResolveBinding != null)
                values.Add((field, null, binding.GetValue<string>()));
            else values.Add((field, graph.ReadField(field.Field.FieldType, value,
                field.Field.IsDefined(typeof(SerializeReferenceAttribute))), null));
        }
        return values;
    }

    private static void ApplyFields(InstanceEntry entry, List<(FieldEntry field, object? value, string? slot)> values) {
        foreach ((FieldEntry field, object? value, string? slot) in values) {
            entry.bindingFields.RemoveAll(binding => binding.field == field);
            if (slot != null) entry.bindingFields.Add((field, slot));
            else field.Set(entry.clip, value);
        }
        entry.bindingsPrepared = false;
    }

    // 再生中にReflectionを使わない生成処理を用意する
    private static TypeEntry Describe(Type type) {
        ConstructorInfo constructor = type.GetConstructor(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance,
            null, Type.EmptyTypes, null) ?? throw new InvalidOperationException("引数なしコンストラクターが必要です");
        Func<TimelineClip> create = Expression.Lambda<Func<TimelineClip>>(Expression.New(constructor)).Compile();
        var fields = new List<FieldEntry>();
        for (Type? current = type; current != null && current != typeof(TimelineClip); current = current.BaseType) {
            foreach (FieldInfo field in current.GetFields(BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic |
                BindingFlags.DeclaredOnly)) {
                if (field.IsStatic || field.IsInitOnly || field.IsDefined(typeof(NonSerializedAttribute)) ||
                    (!field.IsPublic && !field.IsDefined(typeof(SerializeFieldAttribute)) &&
                        !field.IsDefined(typeof(SerializeReferenceAttribute)))) continue;
                string id = field.GetCustomAttributesData().FirstOrDefault(attribute =>
                    attribute.AttributeType == typeof(SerializedFieldIDAttribute))?.ConstructorArguments.FirstOrDefault().Value as string
                    ?? field.Name;
                if (fields.Any(entry => entry.ID == id || entry.Field.Name == field.Name))
                    throw new InvalidOperationException($"保存Field名が重複しています: {field.Name}");
                var target = Expression.Parameter(typeof(TimelineClip));
                var value = Expression.Parameter(typeof(object));
                var assign = Expression.Assign(Expression.Field(Expression.Convert(target, field.DeclaringType!), field),
                    Expression.Convert(value, field.FieldType));
                Func<GameObject, object?>? bindingResolver = null;
                if (field.FieldType == typeof(GameObject)) bindingResolver = owner => owner;
                else if (typeof(Component).IsAssignableFrom(field.FieldType) || typeof(MonoBehaviour).IsAssignableFrom(field.FieldType)) {
                    var owner = Expression.Parameter(typeof(GameObject));
                    MethodInfo getter = typeof(GameObject).GetMethod(nameof(GameObject.GetComponent), BindingFlags.Instance |
                        BindingFlags.Public)!.MakeGenericMethod(field.FieldType);
                    bindingResolver = Expression.Lambda<Func<GameObject, object?>>(
                        Expression.Convert(Expression.Call(owner, getter), typeof(object)), owner).Compile();
                }
                string[] formerNames = field.GetCustomAttributesData().Where(attribute =>
                    attribute.AttributeType == typeof(FormerlySerializedAsAttribute)).Select(attribute =>
                    (string)attribute.ConstructorArguments[0].Value!).ToArray();
                fields.Add(new FieldEntry(id, field, Expression.Lambda<Action<TimelineClip, object?>>(
                    Expression.Block(assign, Expression.Empty()), target, value).Compile(), bindingResolver, formerNames));
            }
        }
        var attribute = type.GetCustomAttribute<TimelineClipTypeAttribute>();
        return new TypeEntry(attribute?.ID ?? type.FullName!, type.FullName!, attribute?.DisplayName ?? type.Name,
            attribute?.SupportsPreview ?? false, create, fields.ToArray());
    }

    // Editorへ保存Fieldと既定値を渡す
    private static JsonObject CreateSchema(TypeEntry entry) {
        TimelineClip defaults = entry.Create();
        var options = ScriptFieldCodec.CreateJSONOptions(new ScriptReferenceContext());
        var fields = new JsonArray();
        foreach (FieldEntry field in entry.Fields) {
            JsonNode? defaultValue = null;
            try { defaultValue = JsonSerializer.SerializeToNode(field.Field.GetValue(defaults), field.Field.FieldType, options); }
            catch { }
            fields.Add(TimelineClipFieldSchema.Describe(field.Field, field.ID, defaultValue));
        }
        return new JsonObject { ["id"] = entry.ID, ["typeName"] = entry.Name, ["displayName"] = entry.DisplayName,
            ["supportsPreview"] = entry.SupportsPreview, ["fields"] = fields };
    }
}
