using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed partial class TextCharAnimation {

    private static readonly JsonSerializerOptions ReferenceOptions = new() { IncludeFields = true };

    public Object? TranslationSource { get => ReadSource(0); set => SetSource(0, value); }
    public Object? RotationSource { get => ReadSource(1); set => SetSource(1, value); }
    public Object? ScaleSource { get => ReadSource(2); set => SetSource(2, value); }
    public Object? ColorSource { get => ReadSource(3); set => SetSource(3, value); }

    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 81);
        set => ComponentTimeSource.Apply(native, 81, value);
    }

    public float Interval {
        get => IntervalSeconds;
        set => IntervalSeconds = ValidateSeconds(value);
    }

    public float TotalDuration {
        get => TotalSeconds;
        set => TotalSeconds = ValidateSeconds(value);
    }

    public EasingType Easing {
        get => TimingEasing;
        set {
            if (value is not (>= EasingType.Linear and <= EasingType.EaseInCirc or
                >= EasingType.EaseOutSine and <= EasingType.EaseOutCirc or
                >= EasingType.EaseInOutSine and <= EasingType.EaseInOutCirc))
                throw new ArgumentOutOfRangeException(nameof(value));
            TimingEasing = value;
        }
    }

    public void Play() => ControlRequest = 1;
    public void Pause() => ControlRequest = 2;
    public void Resume() => ControlRequest = 3;
    public void Stop() => ControlRequest = 4;

    public void Seek(float seconds) {
        SeekTimeRequest = ValidateSeconds(seconds);
        ControlRequest = 6;
    }

    private Object? ReadSource(int slot) {
        var reference = ComponentSettingsTransfer.Read(native, 81)["sources"]!.AsArray()[slot];
        if (reference == null) return null;
        if (reference["entity"] is JsonNode entity)
            return GameObject.FromNative(entity.Deserialize<NativeEntity>(ReferenceOptions));
        var id = AssetGUID.Parse(reference["asset"]?.GetValue<string>());
        return reference["type"]?.GetValue<string>() switch {
            "Prefab" => new Prefab(id),
            "AnimationClip" => new AnimationClip(id),
            _ => null
        };
    }

    private void SetSource(int slot, Object? source) {
        var patch = new JsonObject { ["sourceSlot"] = slot };
        switch (source) {
            case null: break;
            case GameObject entity:
                if (!entity) throw new ArgumentException("参照先のオブジェクトが見つかりません", nameof(source));
                patch["entity"] = JsonSerializer.SerializeToNode(GameObject.RawNative(entity), ReferenceOptions);
                break;
            case Prefab or AnimationClip:
                patch["asset"] = ((Asset)source).assetID.ToString();
                break;
            default: throw new ArgumentException("GameObject、Prefab、AnimationClipを指定してください", nameof(source));
        }
        ComponentSettingsTransfer.Apply(native, 81, patch);
    }

    private static float ValidateSeconds(float seconds) {
        if (!float.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        return seconds;
    }
}
