using System.Text.Json;
using System.Text.Json.Nodes;
using System.Globalization;
using System.Text;

namespace NEMEngine;

public enum TimelineEvaluationMode { Playback, Seek, Preview }
public enum TimelineExitReason { Completed, Stopped, Reset, Seek, BindingLost, WorldExit, Reload, Faulted }

// 独自クリップの保存IDと表示名を指定する
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class TimelineClipTypeAttribute(string id, string displayName) : Attribute {
    public string ID { get; } = id;
    public string DisplayName { get; } = displayName;
    public bool SupportsPreview { get; set; }
}

// 保存設定と再生インスタンスを分けて独自処理を記述する
public abstract class TimelineClip {
    public virtual void OnEnter(TimelineContext context) { }
    public abstract void Evaluate(TimelineContext context);
    public virtual void OnExit(TimelineContext context, TimelineExitReason reason) { }
}

// 時刻と対象を借用し評価中の値を提供する
public sealed class TimelineContext {
    private sealed class BindingValue(NativeEntity native) {
        internal NativeEntity native = native;
        internal GameObject? value = GameObject.FromNative(native);
    }
    private sealed class ParameterValue(NativeTimelineValue value, string text) {
        internal NativeTimelineValue value = value;
        internal string text = text;
        internal byte[] utf8 = Encoding.UTF8.GetBytes(text);
    }
    private readonly Dictionary<string, BindingValue> bindings = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ParameterValue> parameters = new(StringComparer.Ordinal);
    private readonly Dictionary<ulong, BindingValue> bindingIDs = new();
    private readonly Dictionary<ulong, ParameterValue> parameterIDs = new();
    private readonly bool usePlayerBindings;
    private bool synchronized;

    internal TimelineContext(NativeEntity owner, JsonObject configuration) {
        Owner = GameObject.FromNative(owner);
        Player = Owner?.GetComponent<TimelinePlayer>();
        usePlayerBindings = Player != null && configuration["usePlayerBindings"]?.GetValue<bool>() != false;
        if (usePlayerBindings && configuration["timeline"] is JsonValue timeline)
            usePlayerBindings = Player!.Timeline?.assetID == AssetGUID.Parse(timeline.GetValue<string>());
        if (configuration["bindings"] is JsonObject sourceBindings) {
            foreach ((string name, JsonNode? source) in sourceBindings) {
                if (source is not JsonObject value) continue;
                NativeEntity entity = value.ContainsKey("worldIndex") ? new NativeEntity {
                    world = new ManagedWorldHandle { index = value["worldIndex"]!.GetValue<uint>(),
                        generation = value["worldGeneration"]!.GetValue<uint>() },
                    index = value["index"]!.GetValue<uint>(), generation = value["generation"]!.GetValue<uint>()
                } : value.Deserialize<NativeEntity>(TimelineTransfer.Options);
                string canonical = value["name"]?.GetValue<string>() ?? name;
                if (!bindings.TryGetValue(canonical, out BindingValue? binding))
                    bindings[canonical] = binding = new(entity);
                bindings[name] = binding;
                if (ReadID(name, value, out ulong id)) {
                    bindingIDs[id] = binding;
                    bindings[id.ToString("x16", CultureInfo.InvariantCulture)] = binding;
                }
            }
        }
        if (configuration["parameters"] is JsonObject sourceParameters) {
            foreach ((string name, JsonNode? source) in sourceParameters) {
                if (source is not JsonObject value) continue;
                JsonNode? item = value["value"];
                string canonical = value["name"]?.GetValue<string>() ?? name;
                if (!parameters.TryGetValue(canonical, out ParameterValue? parameter)) {
                    NativeTimelineValue typed = value["type"]?.GetValue<string>() switch {
                        "Bool" => new() { kind = 0, integer = item?.GetValue<bool>() == true ? 1 : 0 },
                        "Int" => new() { kind = 1, integer = item?.GetValue<int>() ?? 0 },
                        "Float" => new() { kind = 2, scalar = item?.GetValue<float>() ?? 0 },
                        "Vector3" => new() { kind = 3, vector = ReadVector(item) },
                        "String" => new() { kind = 4 }, _ => new() { kind = -1 }
                    };
                    parameters[canonical] = parameter = new(typed,
                        typed.kind == 4 ? item?.GetValue<string>() ?? string.Empty : string.Empty);
                }
                parameters[name] = parameter;
                if (ReadID(name, value, out ulong id)) {
                    parameterIDs[id] = parameter;
                    parameters[id.ToString("x16", CultureInfo.InvariantCulture)] = parameter;
                }
            }
        }
    }

    public GameObject? Owner { get; }
    public TimelinePlayer? Player { get; }
    public double Time { get; internal set; }
    public double LocalTime { get; internal set; }
    public double DeltaTime { get; internal set; }
    public double Duration { get; internal set; }
    public float Weight { get; internal set; } = 1;
    public TimelineEvaluationMode Mode { get; internal set; }
    public bool IsPreview => Mode == TimelineEvaluationMode.Preview;
    public bool IsScrubbing { get; internal set; }

    public GameObject? GetBinding(string name) => usePlayerBindings && !synchronized ?
        Player!.GetBinding(name) : bindings.GetValueOrDefault(name)?.value;
    public float GetFloat(string name) => usePlayerBindings && !synchronized ? Player!.GetFloat(name) : Read(name, 2).scalar;
    public int GetInt(string name) => usePlayerBindings && !synchronized ? Player!.GetInt(name) : Read(name, 1).integer;
    public bool GetBool(string name) => usePlayerBindings && !synchronized ? Player!.GetBool(name) : Read(name, 0).integer != 0;
    public Vector3 GetVector3(string name) => usePlayerBindings && !synchronized ? Player!.GetVector3(name) : Read(name, 3).vector;
    public string GetString(string name) => usePlayerBindings && !synchronized ?
        Player!.GetString(name) : parameters.GetValueOrDefault(name)?.text ?? string.Empty;
    public bool IsWindowOpen(string name) => usePlayerBindings && Player!.IsWindowOpen(name);

    // 再生中のContext参照を保って編集済みの参照定義を取り込む
    internal void ApplyConfiguration(TimelineContext source) {
        bindings.Clear();
        parameters.Clear();
        bindingIDs.Clear();
        parameterIDs.Clear();
        foreach (var binding in source.bindings) bindings.Add(binding.Key, binding.Value);
        foreach (var parameter in source.parameters) parameters.Add(parameter.Key, parameter.Value);
        foreach (var binding in source.bindingIDs) bindingIDs.Add(binding.Key, binding.Value);
        foreach (var parameter in source.parameterIDs) parameterIDs.Add(parameter.Key, parameter.Value);
        synchronized = false;
    }

    // 世代付き参照と型付きの現在値だけを更新する
    internal unsafe bool Update(ReadOnlySpan<NativeTimelineBindingUpdate> targets,
        ReadOnlySpan<NativeTimelineParameterUpdate> values) {
        bool changed = false;
        foreach (ref readonly NativeTimelineBindingUpdate update in targets) {
            if (!bindingIDs.TryGetValue(update.id, out BindingValue? binding))
                throw new ArgumentException("Timelineの割り当てIDが一致しません");
            NativeEntity native = update.Entity;
            if (binding.native.world.index == native.world.index && binding.native.world.generation == native.world.generation &&
                binding.native.index == native.index && binding.native.generation == native.generation) continue;
            binding.native = native;
            binding.value = GameObject.FromNative(native);
            changed = true;
        }
        foreach (ref readonly NativeTimelineParameterUpdate update in values) {
            if (!parameterIDs.TryGetValue(update.id, out ParameterValue? parameter) || parameter.value.kind != update.value.kind)
                throw new ArgumentException("TimelineのパラメーターIDまたは型が一致しません");
            parameter.value = update.value;
            if (update.value.kind != 4) continue;
            int length = 0;
            if (update.stringValue != null) {
                while (length < 1024 * 1024 && update.stringValue[length] != 0) ++length;
                if (length == 1024 * 1024) throw new ArgumentException("Timelineの文字列が長すぎます");
            }
            ReadOnlySpan<byte> utf8 = new(update.stringValue, length);
            if (utf8.SequenceEqual(parameter.utf8)) continue;
            parameter.utf8 = utf8.ToArray();
            parameter.text = Encoding.UTF8.GetString(utf8);
        }
        synchronized = true;
        return changed;
    }

    private NativeTimelineValue Read(string name, int kind) =>
        parameters.TryGetValue(name, out ParameterValue? value) && value.value.kind == kind ? value.value : default;

    private static bool ReadID(string name, JsonObject value, out ulong id) => ulong.TryParse(
        value["id"]?.GetValue<string>() ?? name, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out id);

    private static Vector3 ReadVector(JsonNode? value) => value is JsonArray array && array.Count >= 3 ?
        new(array[0]!.GetValue<float>(), array[1]!.GetValue<float>(), array[2]!.GetValue<float>()) :
        value?.Deserialize<Vector3>(TimelineTransfer.Options) ?? default;
}
