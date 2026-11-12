using System.Runtime.InteropServices;
using System.Text.Json;

namespace NEMEngine;

// 対象の割り当てと実行中のパラメーターは専用APIで変更する
public sealed class TimelinePlayerSettings {
    public bool Enabled { get; set; } = true;
    public bool PlayOnStart { get; set; }
    public bool PlayInEditMode { get; set; }
    public float Speed { get; set; } = 1;
    public double StartDelay { get; set; }
    public bool Loop { get; set; }
    public int Priority { get; set; }
}

public readonly record struct TimelinePlayerState(double Time, double Duration, bool IsPlaying,
    bool IsPaused, bool IsFinished, float PreparationProgress = 1, bool IsPreparing = false, int PendingEvents = 0);

public sealed class TimelineEvent {
    public string Name { get; set; } = string.Empty;
    public double Time { get; set; }
    public float FloatParam { get; set; }
    public int IntParam { get; set; }
    public string StringParam { get; set; } = string.Empty;
    public GameObject? Target { get; internal set; }
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeTimelineState {
    internal double time;
    internal double duration;
    internal int playing;
    internal int paused;
    internal int finished;
    internal float preparationProgress;
    internal int preparing;
    internal int eventCount;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeTimelineValue {
    internal int kind;
    internal int integer;
    internal float scalar;
    internal float x, y, z;
    internal Vector3 vector {
        readonly get => new(x, y, z);
        set { x = value.x; y = value.y; z = value.z; }
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeTimelineFrame {
    internal double time;
    internal double localTime;
    internal double deltaTime;
    internal double duration;
    internal float weight;
    internal int evaluationMode;
    internal int phase;
    internal int exitReason;
    internal int isScrubbing;
}

[StructLayout(LayoutKind.Sequential)]
public struct NativeTimelineBindingUpdate {
    internal ulong id;
    internal uint worldIndex, worldGeneration, index, generation;
    internal readonly NativeEntity Entity => new() {
        world = new() { index = worldIndex, generation = worldGeneration }, index = index, generation = generation
    };
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeTimelineParameterUpdate {
    internal ulong id;
    internal NativeTimelineValue value;
    internal byte* stringValue;
}

internal static class TimelineTransfer {
    internal static readonly JsonSerializerOptions Options = new(SimpleAnimationSettingsTransfer.Options);
}
