namespace NEMEngine;

// 自動生成されるUISelectableへRuntime状態を追加する
public sealed partial class UISelectable {

    // 現在の選択状態
    public UISelectableState State =>
        (UISelectableState)NativeUIAPI.ReadUISelectableRuntimeState(native).state;

    // 通常状態へ遷移したフレームか
    public bool NormalThisFrame =>
        NativeUIAPI.ReadUISelectableRuntimeState(native).normalThisFrame != 0;

    // 選択状態へ遷移したフレームか
    public bool SelectedThisFrame =>
        NativeUIAPI.ReadUISelectableRuntimeState(native).selectedThisFrame != 0;

    // 決定状態へ遷移したフレームか
    public bool SubmittedThisFrame =>
        NativeUIAPI.ReadUISelectableRuntimeState(native).submittedThisFrame != 0;

    // 無効状態へ遷移したフレームか
    public bool DisabledThisFrame =>
        NativeUIAPI.ReadUISelectableRuntimeState(native).disabledThisFrame != 0;
}
