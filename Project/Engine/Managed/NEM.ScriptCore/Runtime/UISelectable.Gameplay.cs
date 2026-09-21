namespace NEMEngine;

// 自動生成されるUISelectableへRuntime状態を追加する
public sealed partial class UISelectable {

    // 現在の選択状態
    public UISelectableState State =>
        (UISelectableState)NativeAPI.ReadUISelectableRuntimeState(entity.native).state;

    // 通常状態へ遷移したフレームか
    public bool NormalThisFrame =>
        NativeAPI.ReadUISelectableRuntimeState(entity.native).normalThisFrame != 0;

    // 選択状態へ遷移したフレームか
    public bool SelectedThisFrame =>
        NativeAPI.ReadUISelectableRuntimeState(entity.native).selectedThisFrame != 0;

    // 決定状態へ遷移したフレームか
    public bool SubmittedThisFrame =>
        NativeAPI.ReadUISelectableRuntimeState(entity.native).submittedThisFrame != 0;

    // 無効状態へ遷移したフレームか
    public bool DisabledThisFrame =>
        NativeAPI.ReadUISelectableRuntimeState(entity.native).disabledThisFrame != 0;
}
