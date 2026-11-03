using System.Text.Json;

namespace NEMEngine;

public enum MotionAnimationMode { Relative, ApproachTarget, Segments, Spline }
public enum MotionAnimationPlane { Full3D, GroundXZ, PlaneXY }
public enum MotionAnimationRotation { Relative, LookAtTarget, PathTangent }

public sealed class MotionAnimationSegment {
    public Vector3 Offset { get; set; } = new(0, 0, 1);
    public float Duration { get; set; } = 1;
    public EasingType Easing { get; set; } = EasingType.Linear;
}

// Offsetは開始時の向きを基準にした距離。区間のOffsetも共通の開始位置から測る。
public sealed class MotionAnimationSettings {
    public bool Enabled { get; set; } = true;
    public bool PlayOnStart { get; set; } = true;
    public bool PlayInEditMode { get; set; }
    public bool AutoApply { get; set; } = true;
    public bool ApplyPosition { get; set; } = true;
    public bool ApplyRotation { get; set; }
    public bool TrackTarget { get; set; }
    public bool DrawDebug { get; set; } = true;
    public MotionAnimationMode Mode { get; set; }
    public MotionAnimationPlane Plane { get; set; }
    public MotionAnimationRotation Rotation { get; set; }
    public SimpleAnimationRotationMode RotationEdit { get; set; } = SimpleAnimationRotationMode.Euler;
    public SimpleAnimationRepeat Repeat { get; set; }
    public Vector3 Offset { get; set; } = new(0, 0, 1);
    public Vector3 Euler { get; set; }
    public Vector3 Axis { get; set; } = new(0, 1, 0);
    public float Angle { get; set; }
    public float StoppingDistance { get; set; } = 1;
    public float SplineStart { get; set; }
    public float SplineEnd { get; set; } = 1;
    public float Duration { get; set; } = 1;
    public float Delay { get; set; }
    public float Speed { get; set; } = 1;
    public uint CycleCount { get; set; }
    public EasingType Easing { get; set; } = EasingType.Linear;
    public MotionAnimationSegment[] Segments { get; set; } = [new()];
}

public sealed class MotionAnimationState {
    public double Time { get; set; }
    public Vector3 Position { get; set; }
    public Quaternion Rotation { get; set; }
    public Vector3 DeltaPosition { get; set; }
    public Quaternion DeltaRotation { get; set; }
    public bool IsPlaying { get; set; }
    public bool IsPaused { get; set; }
    public bool IsFinished { get; set; }
    public ulong Cycles { get; set; }
    public string Diagnostic { get; set; } = string.Empty;
}

// 区間番号ごとの通過回数を返す。1フレームに複数区間を通っても失われない。
public sealed class MotionAnimationEvents {
    public ulong[] Segments { get; set; } = [];
    public ulong Cycles { get; set; }
    public ulong Completions { get; set; }
}

internal static class MotionAnimationTransfer {
    internal static readonly JsonSerializerOptions Options = CreateOptions();
    private static JsonSerializerOptions CreateOptions() {
        var options = new JsonSerializerOptions(SimpleAnimationSettingsTransfer.Options);
        options.Converters.Clear();
        return options;
    }
}
