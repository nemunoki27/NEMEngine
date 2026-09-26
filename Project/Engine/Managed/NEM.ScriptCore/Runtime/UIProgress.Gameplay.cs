namespace NEMEngine;

// 自動生成されるUIProgressへRuntime表示値を追加する
public sealed partial class UIProgress {

    // 補間後の現在表示値
    public float DisplayedNormalizedValue =>
        NativeUIAPI.ReadUIProgressRuntimeState(native).displayedValue;

    // 遅延表示側の現在値
    public float DelayedNormalizedValue =>
        NativeUIAPI.ReadUIProgressRuntimeState(native).delayedValue;
}
