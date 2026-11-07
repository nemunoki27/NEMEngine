using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

[Flags]
public enum TimeChannel : uint {
    SkinnedAnimation = 1, Particle = 2, SimpleAnimation = 4,
    AnimationClip = 8, Flipbook = 16, MotionAnimation = 32, Timer = 64, DayNightCycle = 128, All = 255
}
public enum TimeSourceMode { Hierarchy, Explicit, Global, Unscaled }

public sealed class TimeSourceSettings {
    public TimeSourceMode Mode { get; set; } = TimeSourceMode.Hierarchy;
    public GameObject? Source { get; set; }
}

internal static class ComponentTimeSource {
    private static readonly JsonSerializerOptions Options = new() { IncludeFields = true };

    internal static TimeSourceSettings Read(NativeEntity entity, int type) {
        var values = ComponentSettingsTransfer.Read(entity, type);
        var settings = values["timeSource"]!.AsObject();
        return new() {
            Mode = Enum.Parse<TimeSourceMode>(settings["mode"]!.GetValue<string>()),
            Source = GameObject.FromNative(values["timeSourceNative"]!.Deserialize<NativeEntity>(Options))
        };
    }

    internal static void Apply(NativeEntity entity, int type, TimeSourceSettings settings) {
        ArgumentNullException.ThrowIfNull(settings);
        if (!Enum.IsDefined(settings.Mode)) throw new ArgumentOutOfRangeException(nameof(settings));
        ComponentSettingsTransfer.Apply(entity, type, new JsonObject {
            ["timeSourcePatch"] = true,
            ["timeSource"] = new JsonObject { ["mode"] = settings.Mode.ToString() },
            ["timeSourceNative"] = JsonSerializer.SerializeToNode(GameObject.RawNative(settings.Source), Options)
        });
    }
}

public sealed partial class TimeScale {
    public TimeChannel Channels { get => (TimeChannel)ChannelMask; set => ChannelMask = (uint)value & (uint)TimeChannel.All; }
    public float GetDeltaTime(TimeChannel channel = TimeChannel.SimpleAnimation) => NativeApplicationAPI.ReadLocalDeltaTime(native, channel);
    public float GetEffectiveScale(TimeChannel channel = TimeChannel.SimpleAnimation) => NativeApplicationAPI.ReadLocalTimeScale(native, channel);
}

public sealed partial class AnimationClipPlayer {
    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 0);
        set => ComponentTimeSource.Apply(native, 0, value);
    }
}

public sealed partial class ParticleSystem {
    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 8);
        set => ComponentTimeSource.Apply(native, 8, value);
    }
}

public sealed partial class FlipbookAnimation {
    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 9);
        set => ComponentTimeSource.Apply(native, 9, value);
    }
}

public sealed partial class SkinnedAnimation {
    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 26);
        set => ComponentTimeSource.Apply(native, 26, value);
    }
}

public sealed partial class SimpleAnimation {
    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 70);
        set => ComponentTimeSource.Apply(native, 70, value);
    }
}
