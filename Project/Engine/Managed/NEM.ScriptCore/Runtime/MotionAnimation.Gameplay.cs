using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed partial class MotionAnimation {
    public MotionAnimationSettings GetSettings() => ComponentSettingsTransfer.Read(native, 72)
        .Deserialize<MotionAnimationSettings>(MotionAnimationTransfer.Options)!;

    public void SetSettings(MotionAnimationSettings settings) {
        ArgumentNullException.ThrowIfNull(settings);
        ComponentSettingsTransfer.Apply(native, 72,
            JsonSerializer.SerializeToNode(settings, MotionAnimationTransfer.Options)!.AsObject());
    }

    public TimeSourceSettings TimeSource {
        get => ComponentTimeSource.Read(native, 72);
        set => ComponentTimeSource.Apply(native, 72, value);
    }

    // 複数の現在値が必要な場合は、このSnapshotを一度取得して使う。
    public MotionAnimationState GetState() => ComponentSettingsTransfer.Read(native, 72)["runtime"]!
        .Deserialize<MotionAnimationState>(MotionAnimationTransfer.Options)!;
    public Vector3 CurrentPosition => GetState().Position;
    public Quaternion CurrentRotation => GetState().Rotation;
    public Vector3 DeltaPosition => GetState().DeltaPosition;
    public Quaternion DeltaRotation => GetState().DeltaRotation;
    public bool IsPlaying => GetState().IsPlaying;
    public bool IsPaused => GetState().IsPaused;
    public bool IsFinished => GetState().IsFinished;
    public double CurrentTime => GetState().Time;

    public void Play() => Command(1);
    public void Pause() => Command(2);
    public void Resume() => Command(3);
    public void Stop() => Command(4);
    public void Reset() => Command(5);
    public void Seek(double seconds) {
        if (!double.IsFinite(seconds) || seconds < 0) throw new ArgumentOutOfRangeException(nameof(seconds));
        Command(6, seconds);
    }
    public void RecaptureOrigin() => Command(7);
    public void FreezeTarget() => Command(8);
    public void CaptureTarget() => Command(9);

    public void SetTarget(GameObject? target, bool capture = true) => ComponentSettingsTransfer.Apply(native, 72,
        new JsonObject { ["targetNative"] = JsonSerializer.SerializeToNode(GameObject.RawNative(target), MotionAnimationTransfer.Options), ["capture"] = capture });
    public void SetSpline(GameObject? spline) => ComponentSettingsTransfer.Apply(native, 72,
        new JsonObject { ["splineNative"] = JsonSerializer.SerializeToNode(GameObject.RawNative(spline), MotionAnimationTransfer.Options) });

    // Seekでは通知を追加しない。取得した通知はこの呼び出しで消費する。
    public MotionAnimationEvents ConsumeEvents() {
        var events = ComponentSettingsTransfer.Read(native, 72)["events"]!.Deserialize<MotionAnimationEvents>(MotionAnimationTransfer.Options)!;
        Command(10);
        return events;
    }

    private void Command(int command, double time = 0) => ComponentSettingsTransfer.Apply(native, 72,
        new JsonObject { ["command"] = command, ["time"] = time });
}
