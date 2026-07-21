namespace NEMEngine;

// 自動生成されるIrisTransition wrapperの再生操作
public sealed partial class IrisTransition {

    public bool IsPlaying =>
        State == IrisTransitionState.IrisOut ||
        State == IrisTransitionState.IrisIn;

    public bool IsCovered => State == IrisTransitionState.Covered;

    public void IrisOut(float progress = 0.0f) =>
        NativeApi.IrisTransitionCommandCall(entity.native, 0, progress);

    public void IrisIn(float progress = 1.0f) =>
        NativeApi.IrisTransitionCommandCall(entity.native, 1, progress);

    public void SetProgress(float progress) =>
        NativeApi.IrisTransitionCommandCall(entity.native, 2, progress);

    public void Cancel() =>
        NativeApi.IrisTransitionCommandCall(entity.native, 3);

    public void Reset() =>
        NativeApi.IrisTransitionCommandCall(entity.native, 4);
}
