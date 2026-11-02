using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed partial class UISelectable {

    public SimpleAnimationSettings<Color4> GetColorAnimation(UISelectableState state) => ReadAnimation<Color4>(state, "colorAnimation");
    public SimpleAnimationSettings<Vector2> GetScaleAnimation(UISelectableState state) => ReadAnimation<Vector2>(state, "scaleAnimation");
    public void SetColorAnimation(UISelectableState state, SimpleAnimationSettings<Color4> settings) => WriteAnimation(state, "colorAnimation", settings);
    public void SetScaleAnimation(UISelectableState state, SimpleAnimationSettings<Vector2> settings) => WriteAnimation(state, "scaleAnimation", settings);
    public Color4 CurrentColor => NativeUIAPI.ReadUISelectableRuntimeState(native).currentColor;
    public Vector2 CurrentScale => NativeUIAPI.ReadUISelectableRuntimeState(native).currentScale;

    private static string StateKey(UISelectableState state) => state switch {
        UISelectableState.Normal => "normal", UISelectableState.Selected => "selected",
        UISelectableState.Submitted => "submitted", UISelectableState.Disabled => "disabled",
        _ => throw new ArgumentOutOfRangeException(nameof(state))
    };

    private SimpleAnimationSettings<T> ReadAnimation<T>(UISelectableState state, string key) where T : unmanaged =>
        SimpleAnimationSettingsTransfer.Read<T>(ComponentSettingsTransfer.Read(native, 35)[StateKey(state)]![key]!.AsObject());

    private void WriteAnimation<T>(UISelectableState state, string key, SimpleAnimationSettings<T> settings) where T : unmanaged {
        JsonObject values = ComponentSettingsTransfer.Read(native, 35);
        values[StateKey(state)]![key] = SimpleAnimationSettingsTransfer.Write(settings);
        ComponentSettingsTransfer.Apply(native, 35, values);
    }
}
