namespace NEMEngine;

// 自動生成されるUIProgressへRuntime表示値を追加する
public sealed partial class UIProgress {

    // 補間後の現在表示値
    public float DisplayedNormalizedValue =>
        NativeAPI.ReadUIProgressRuntimeState(entity.native).displayedValue;

    // 遅延表示側の現在値
    public float DelayedNormalizedValue =>
        NativeAPI.ReadUIProgressRuntimeState(entity.native).delayedValue;
}
