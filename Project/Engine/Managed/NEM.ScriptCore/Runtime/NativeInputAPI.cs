using System.Text;

namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeInputAPI {

    internal static bool ReadKey(int key) {
        return GetKey != null && GetKey(key) != 0;
    }

    internal static bool ReadKeyDown(int key) {
        return GetKeyDown != null && GetKeyDown(key) != 0;
    }

    internal static bool ReadKeyUp(int key) {
        return GetKeyUp != null && GetKeyUp(key) != 0;
    }

    internal static bool ReadMouseButton(int button) {
        return GetMouseButton != null && GetMouseButton(button) != 0;
    }

    internal static bool ReadMouseButtonDown(int button) {
        return GetMouseButtonDown != null && GetMouseButtonDown(button) != 0;
    }

    internal static bool ReadMouseButtonUp(int button) {
        return GetMouseButtonUp != null && GetMouseButtonUp(button) != 0;
    }

    internal static Vector2 ReadMousePosition() {
        return GetMousePosition != null ? GetMousePosition().ToVector2() : Vector2.zero;
    }

    internal static Vector2 ReadMouseDelta() {
        return GetMouseDelta != null ? GetMouseDelta().ToVector2() : Vector2.zero;
    }

    internal static float ReadMouseWheel() {
        return GetMouseWheel != null ? GetMouseWheel() : 0.0f;
    }

    internal static bool ReadGamepadButton(int button) {
        return GetGamepadButton != null && GetGamepadButton(button) != 0;
    }

    internal static bool ReadGamepadButtonDown(int button) {
        return GetGamepadButtonDown != null && GetGamepadButtonDown(button) != 0;
    }

    internal static bool ReadIsGamepadConnected() {
        return IsGamepadConnected != null && IsGamepadConnected() != 0;
    }

    internal static Vector2 ReadLeftStick() {
        return GetLeftStick != null ? GetLeftStick().ToVector2() : Vector2.zero;
    }

    internal static Vector2 ReadRightStick() {
        return GetRightStick != null ? GetRightStick().ToVector2() : Vector2.zero;
    }

    internal static float ReadLeftTrigger() {
        return GetLeftTrigger != null ? GetLeftTrigger() : 0.0f;
    }

    internal static float ReadRightTrigger() {
        return GetRightTrigger != null ? GetRightTrigger() : 0.0f;
    }

    internal static int ReadInputType() => GetInputType != null ? GetInputType() : 0;

    internal static bool ReadMouseRangeControl() => GetMouseRangeControl != null && GetMouseRangeControl() != 0;

    internal static void WriteMouseRangeControl(bool enabled) { if (SetMouseRangeControl != null) { SetMouseRangeControl(enabled ? 1 : 0); } }

    internal static bool ReadMousePositionInView(out Vector2 position) {

        position = Vector2.zero;
        if (GetMousePositionInView == null) {
            return false;
        }
        NativeVector2 nativePosition = default;
        if (GetMousePositionInView(&nativePosition) == 0) {
            return false;
        }
        position = nativePosition.ToVector2();
        return true;
    }

    internal static bool ReadGamepadButton(int index, int button) => GetGamepadButtonIndexed != null && GetGamepadButtonIndexed(index, button) != 0;

    internal static bool ReadGamepadButtonDown(int index, int button) => GetGamepadButtonDownIndexed != null && GetGamepadButtonDownIndexed(index, button) != 0;

    internal static bool ReadGamepadButtonUp(int index, int button) => GetGamepadButtonUpIndexed != null && GetGamepadButtonUpIndexed(index, button) != 0;

    internal static float ReadGamepadAxis(int index, int axis) => GetGamepadAxisIndexed != null ? GetGamepadAxisIndexed(index, axis) : 0.0f;

    internal static bool ReadGamepadConnected(int index) => IsGamepadConnectedIndexed != null && IsGamepadConnectedIndexed(index) != 0;

    internal static int ReadConnectedGamepadCount() => GetConnectedGamepadCount != null ? GetConnectedGamepadCount() : 0;

    internal static string ReadTextInput() {
        if (CopyTextInput == null) {
            return string.Empty;
        }
        int needed = CopyTextInput(null, 0);
        if (needed <= 0) {
            return string.Empty;
        }
        byte[] bytes = new byte[needed + 1];
        fixed (byte* ptr = bytes) {
            int written = CopyTextInput(ptr, needed + 1);
            return written <= 0 ? string.Empty : Encoding.UTF8.GetString(bytes, 0, written);
        }
    }
}
