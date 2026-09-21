namespace NEMEngine;

// gameplay 時間。値は native の frame context / time service が保持し、ここでは cached frame 値を読むだけ。
// hot path で reflection / JSON / file I/O を行わない。scaled と unscaled を明確に分離する。
public static class Time {

    // time scale 適用後のフレーム間秒数（TimeScale=0 で 0 になる）
    public static float DeltaTime => NativeAPI.ReadDeltaTime();

    // time scale を無視したフレーム間秒数（pause 中も実時間で進む）
    public static float UnscaledDeltaTime => NativeAPI.ReadUnscaledDeltaTime();

    // 固定ステップ間隔。値は time scale に依らず一定（scale は substep 回数側に反映される）
    public static float FixedDeltaTime => NativeAPI.ReadFixedDeltaTime();
    public static float UnscaledFixedDeltaTime => NativeAPI.ReadUnscaledFixedDeltaTime();

    // Play 開始からの経過秒数（scaled / unscaled）
    public static double TimeSinceStartup => NativeAPI.ReadTimeSinceStartup();
    public static double UnscaledTime => NativeAPI.ReadUnscaledTime();

    // 全体時間スケール。0 で pause 相当。負値/NaN/infinity は native 側で安全に丸められる。
    // set しても authoring(TimeScaleComponent)へは書き戻さない（runtime 専用）。
    public static float TimeScale {
        get => NativeAPI.ReadTimeScale();
        set => NativeAPI.WriteTimeScale(value);
    }

    // Play 開始からの advance フレーム数
    public static ulong FrameCount => NativeAPI.ReadFrameCount();
}
