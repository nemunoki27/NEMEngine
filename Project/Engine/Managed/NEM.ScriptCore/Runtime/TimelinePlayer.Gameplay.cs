using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed unsafe partial class TimelinePlayer {
    private const int ComponentType = 76;
    private readonly Dictionary<string, byte[]> names = new(StringComparer.Ordinal);

    public TimelinePlayerSettings GetSettings() => ComponentSettingsTransfer.Read(native, ComponentType)
        .Deserialize<TimelinePlayerSettings>(TimelineTransfer.Options)!;

    public void SetSettings(TimelinePlayerSettings settings) {
        ArgumentNullException.ThrowIfNull(settings);
        if (!float.IsFinite(settings.Speed) || settings.Speed < 0 ||
            !double.IsFinite(settings.StartDelay) || settings.StartDelay < 0)
            throw new ArgumentOutOfRangeException(nameof(settings));
        ComponentSettingsTransfer.Apply(native, ComponentType,
            JsonSerializer.SerializeToNode(settings, TimelineTransfer.Options)!.AsObject());
    }

    public TimelineAsset? Timeline {
        get {
            string text = ComponentSettingsTransfer.Read(native, ComponentType)["timeline"]?.GetValue<string>() ?? string.Empty;
            AssetGUID id = AssetGUID.Parse(text);
            return id.isValid ? new TimelineAsset(id) : null;
        }
        set => ComponentSettingsTransfer.Apply(native, ComponentType,
            new JsonObject { ["timeline"] = value?.assetID.ToString() ?? string.Empty });
    }

    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, ComponentType);
        set => ComponentTimeSource.Apply(native, ComponentType, value);
    }

    public bool Enabled { get => GetSettings().Enabled; set => Patch("enabled", value); }
    public bool PlayOnStart { get => GetSettings().PlayOnStart; set => Patch("playOnStart", value); }
    public bool PlayInEditMode { get => GetSettings().PlayInEditMode; set => Patch("playInEditMode", value); }
    public float Speed {
        get => GetSettings().Speed;
        set {
            if (!float.IsFinite(value) || value < 0) throw new ArgumentOutOfRangeException(nameof(value));
            Patch("speed", value);
        }
    }
    public bool Loop { get => GetSettings().Loop; set => Patch("loop", value); }
    public int Priority { get => GetSettings().Priority; set => Patch("priority", value); }
    public double StartDelay {
        get => GetSettings().StartDelay;
        set {
            if (!double.IsFinite(value) || value < 0) throw new ArgumentOutOfRangeException(nameof(value));
            Patch("startDelay", value);
        }
    }

    public TimelinePlayerState GetState() {
        NativeTimelineState state = default;
        if (NativeAPI.GetTimelineState == null || NativeAPI.GetTimelineState(native, &state) == 0)
            throw new MissingReferenceException("TimelinePlayerの状態を取得できません");
        return new(state.time, state.duration, state.playing != 0, state.paused != 0, state.finished != 0,
            state.preparationProgress, state.preparing != 0, state.eventCount);
    }

    public double CurrentTime => GetState().Time;
    public double Duration => GetState().Duration;
    public bool IsPlaying => GetState().IsPlaying;
    public bool IsPaused => GetState().IsPaused;
    public bool IsFinished => GetState().IsFinished;
    public bool IsPreparing => GetState().IsPreparing;
    public float PreparationProgress => GetState().PreparationProgress;
    public void Play() => Command(1);
    public void Play(TimelineAsset timeline) { ArgumentNullException.ThrowIfNull(timeline); Timeline = timeline; Play(); }
    public void Pause() => Command(2);
    public void Resume() => Command(3);
    public void Stop() => Command(4);
    public void Reset() => Command(5);
    public void Seek(double seconds) {
        if (!double.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        Command(6, seconds);
    }

    public GameObject? GetBinding(string name) {
        if (NativeAPI.GetTimelineBinding == null) throw new NotSupportedException("Timeline APIが接続されていません");
        fixed (byte* key = Name(name)) return GameObject.FromNative(NativeAPI.GetTimelineBinding(native, key));
    }

    public void SetBinding(string name, GameObject? target) {
        if (NativeAPI.SetTimelineBinding == null) throw new NotSupportedException("Timeline APIが接続されていません");
        fixed (byte* key = Name(name)) {
            if (NativeAPI.SetTimelineBinding(native, key, GameObject.RawNative(target)) == 0)
                throw new ArgumentException("Timelineの対象名またはComponentが一致しません", nameof(name));
        }
    }

    public bool IsWindowOpen(string name) {
        if (NativeAPI.IsTimelineWindowOpen == null) return false;
        fixed (byte* key = Name(name)) return NativeAPI.IsTimelineWindowOpen(native, key) != 0;
    }

    public void SetFloat(string name, float value) {
        if (!float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(value));
        WriteValue(name, new NativeTimelineValue { kind = 2, scalar = value });
    }
    public void SetInt(string name, int value) => WriteValue(name, new NativeTimelineValue { kind = 1, integer = value });
    public void SetBool(string name, bool value) => WriteValue(name, new NativeTimelineValue { kind = 0, integer = value ? 1 : 0 });
    public void SetVector3(string name, Vector3 value) {
        if (!float.IsFinite(value.x) || !float.IsFinite(value.y) || !float.IsFinite(value.z))
            throw new ArgumentOutOfRangeException(nameof(value));
        WriteValue(name, new NativeTimelineValue { kind = 3, vector = value });
    }
    public float GetFloat(string name) => ReadValue(name, 2).scalar;
    public int GetInt(string name) => ReadValue(name, 1).integer;
    public bool GetBool(string name) => ReadValue(name, 0).integer != 0;
    public Vector3 GetVector3(string name) => ReadValue(name, 3).vector;
    public void SetParameter(string name, float value) => SetFloat(name, value);
    public void SetParameter(string name, int value) => SetInt(name, value);
    public void SetParameter(string name, bool value) => SetBool(name, value);
    public void SetParameter(string name, Vector3 value) => SetVector3(name, value);
    public void SetParameter(string name, string value) => SetString(name, value);

    public void SetString(string name, string value) {
        ArgumentNullException.ThrowIfNull(value);
        if (NativeAPI.SetTimelineStringParameter == null) throw new NotSupportedException("Timeline APIが接続されていません");
        byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(value);
        fixed (byte* key = Name(name)) fixed (byte* text = bytes) {
            if (NativeAPI.SetTimelineStringParameter(native, key, text) == 0)
                throw new ArgumentException("Timelineのパラメーター名または型が一致しません", nameof(name));
        }
    }

    public string GetString(string name) {
        if (NativeAPI.CopyTimelineStringParameter == null) throw new NotSupportedException("Timeline APIが接続されていません");
        fixed (byte* key = Name(name)) {
            int size = NativeAPI.CopyTimelineStringParameter(native, key, null, 0);
            if (size < 0 || size > 1024 * 1024) throw new ArgumentException("Timelineのパラメーター名または型が一致しません", nameof(name));
            byte[] bytes = new byte[size + 1];
            fixed (byte* output = bytes) {
                int copied = NativeAPI.CopyTimelineStringParameter(native, key, output, bytes.Length);
                if (copied < 0 || copied > size) throw new InvalidOperationException("Timelineの文字列を取得できません");
                return Encoding.UTF8.GetString(bytes, 0, copied);
            }
        }
    }

    // 取得した通知だけを消費し次の固定更新の通知を残す
    public TimelineEvent[] ConsumeEvents() {
        if (GetState().PendingEvents == 0) return [];
        JsonObject snapshot = ComponentSettingsTransfer.Read(native, ComponentType);
        var result = new List<TimelineEvent>();
        foreach (JsonNode? value in snapshot["events"]?.AsArray() ?? []) {
            if (value == null) continue;
            TimelineEvent item = value.Deserialize<TimelineEvent>(TimelineTransfer.Options)!;
            if (value["targetNative"] is JsonNode target)
                item.Target = GameObject.FromNative(target.Deserialize<NativeEntity>(TimelineTransfer.Options));
            result.Add(item);
        }
        ComponentSettingsTransfer.Apply(native, ComponentType, new JsonObject { ["consumeEvents"] = true });
        return result.ToArray();
    }

    private byte[] Name(string name) {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        if (name.Length > 1024 || name.Contains('\0')) throw new ArgumentException("Timelineの名前が不正です", nameof(name));
        if (!names.TryGetValue(name, out byte[]? bytes)) names.Add(name, bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name));
        return bytes;
    }
    private void Command(int command, double time = 0) {
        if (NativeAPI.TimelineCommand == null || NativeAPI.TimelineCommand(native, command, time) == 0)
            throw new InvalidOperationException("TimelinePlayerを操作できません");
    }
    private NativeTimelineValue ReadValue(string name, int kind) {
        NativeTimelineValue value = default;
        if (NativeAPI.GetTimelineParameter == null) throw new NotSupportedException("Timeline APIが接続されていません");
        fixed (byte* key = Name(name)) {
            if (NativeAPI.GetTimelineParameter(native, key, &value) == 0 || value.kind != kind)
                throw new ArgumentException("Timelineのパラメーター名または型が一致しません", nameof(name));
        }
        return value;
    }
    private void WriteValue(string name, NativeTimelineValue value) {
        if (NativeAPI.SetTimelineParameter == null) throw new NotSupportedException("Timeline APIが接続されていません");
        fixed (byte* key = Name(name)) {
            if (NativeAPI.SetTimelineParameter(native, key, &value) == 0)
                throw new ArgumentException("Timelineのパラメーター名または型が一致しません", nameof(name));
        }
    }
    private void Patch<T>(string name, T value) => ComponentSettingsTransfer.Apply(native, ComponentType,
        new JsonObject { [name] = JsonSerializer.SerializeToNode(value, TimelineTransfer.Options) });
}
