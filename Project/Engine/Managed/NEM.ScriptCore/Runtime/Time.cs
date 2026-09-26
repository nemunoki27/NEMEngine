namespace NEMEngine;

// gameplay 時間。値は native の frame context / time service が保持し、ここでは cached frame 値を読むだけ。
// hot path で reflection / JSON / file I/O を行わない。scaled と unscaled を明確に分離する。
public static class Time {

    // time scale 適用後のフレーム間秒数（TimeScale=0 で 0 になる）
    public static float deltaTime => NativeApplicationAPI.ReadDeltaTime();

    // time scale を無視したフレーム間秒数（pause 中も実時間で進む）
    public static float unscaledDeltaTime => NativeApplicationAPI.ReadUnscaledDeltaTime();

    // 固定ステップ間隔。値は time scale に依らず一定（scale は substep 回数側に反映される）
    public static float fixedDeltaTime => NativeApplicationAPI.ReadFixedDeltaTime();
    public static float fixedUnscaledDeltaTime => NativeApplicationAPI.ReadUnscaledFixedDeltaTime();

    // Play 開始からの経過秒数（scaled / unscaled）
    public static double timeAsDouble => NativeApplicationAPI.ReadTimeSinceStartup();
    public static double unscaledTimeAsDouble => NativeApplicationAPI.ReadUnscaledTime();

    // フレーム先頭の経過秒数
    public static float time => (float)timeAsDouble;
    public static float unscaledTime => (float)unscaledTimeAsDouble;

    // 全体時間スケール。0 で pause 相当。負値/NaN/infinity は native 側で安全に丸められる。
    // set しても authoring(TimeScaleComponent)へは書き戻さない（runtime 専用）。
    public static float timeScale {
        get => NativeApplicationAPI.ReadTimeScale();
        set => NativeApplicationAPI.WriteTimeScale(value);
    }

    // Play 開始からの advance フレーム数
    public static int frameCount => unchecked((int)NativeApplicationAPI.ReadFrameCount());
}
