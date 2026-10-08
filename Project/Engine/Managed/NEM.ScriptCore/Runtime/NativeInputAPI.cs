namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeInputAPI {

    // キーの押下状態を返す
    internal static bool ReadKey(int key) {

        return GetKey != null && GetKey(key) != 0;
    }

    // キーの押下開始を返す
    internal static bool ReadKeyDown(int key) {

        return GetKeyDown != null && GetKeyDown(key) != 0;
    }

    // キーの解放を返す
    internal static bool ReadKeyUp(int key) {

        return GetKeyUp != null && GetKeyUp(key) != 0;
    }

    // マウスボタンの押下状態を返す
    internal static bool ReadMouseButton(int button) {

        return GetMouseButton != null && GetMouseButton(button) != 0;
    }

    // マウスボタンの押下開始を返す
    internal static bool ReadMouseButtonDown(int button) {

        return GetMouseButtonDown != null && GetMouseButtonDown(button) != 0;
    }

    // マウスボタンの解放を返す
    internal static bool ReadMouseButtonUp(int button) {

        return GetMouseButtonUp != null && GetMouseButtonUp(button) != 0;
    }

    // マウス位置を返す
    internal static Vector2 ReadMousePosition() {

        return GetMousePosition != null ? GetMousePosition().ToVector2() : Vector2.zero;
    }

    // マウスの移動量を返す
    internal static Vector2 ReadMouseDelta() {

        return GetMouseDelta != null ? GetMouseDelta().ToVector2() : Vector2.zero;
    }

    // ホイールの移動量を返す
    internal static float ReadMouseWheel() {

        return GetMouseWheel != null ? GetMouseWheel() : 0.0f;
    }

    // パッドボタンの押下状態を返す
    internal static bool ReadGamepadButton(int button) {

        return GetGamepadButton != null && GetGamepadButton(button) != 0;
    }

    // パッドボタンの押下開始を返す
    internal static bool ReadGamepadButtonDown(int button) {

        return GetGamepadButtonDown != null && GetGamepadButtonDown(button) != 0;
    }

    // 先頭パッドの接続状態を返す
    internal static bool ReadIsGamepadConnected() {

        return IsGamepadConnected != null && IsGamepadConnected() != 0;
    }

    // 左スティックの値を返す
    internal static Vector2 ReadLeftStick() {

        return GetLeftStick != null ? GetLeftStick().ToVector2() : Vector2.zero;
    }

    // 右スティックの値を返す
    internal static Vector2 ReadRightStick() {

        return GetRightStick != null ? GetRightStick().ToVector2() : Vector2.zero;
    }

    // 左トリガーの値を返す
    internal static float ReadLeftTrigger() {

        return GetLeftTrigger != null ? GetLeftTrigger() : 0.0f;
    }

    // 右トリガーの値を返す
    internal static float ReadRightTrigger() {

        return GetRightTrigger != null ? GetRightTrigger() : 0.0f;
    }

    // 直近の入力機器を返す
    internal static int ReadInputType() => GetInputType != null ? GetInputType() : 0;

    // マウス移動範囲の制限状態を返す
    internal static bool ReadMouseRangeControl() => GetMouseRangeControl != null && GetMouseRangeControl() != 0;

    // マウス移動範囲を制限する
    internal static void WriteMouseRangeControl(bool enabled) {

        if (SetMouseRangeControl != null) {
            SetMouseRangeControl(enabled ? 1 : 0);
        }
    }

    // View内のマウス位置を取得する
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

    // パッドボタンの押下状態を返す
    internal static bool ReadGamepadButton(int index, int button) =>
        GetGamepadButtonIndexed != null && GetGamepadButtonIndexed(index, button) != 0;

    // パッドボタンの押下開始を返す
    internal static bool ReadGamepadButtonDown(int index, int button) =>
        GetGamepadButtonDownIndexed != null && GetGamepadButtonDownIndexed(index, button) != 0;

    // パッドボタンの解放を返す
    internal static bool ReadGamepadButtonUp(int index, int button) =>
        GetGamepadButtonUpIndexed != null && GetGamepadButtonUpIndexed(index, button) != 0;

    // 指定パッドの軸値を返す
    internal static float ReadGamepadAxis(int index, int axis) =>
        GetGamepadAxisIndexed != null ? GetGamepadAxisIndexed(index, axis) : 0.0f;

    // 指定パッドの接続状態を返す
    internal static bool ReadGamepadConnected(int index) =>
        IsGamepadConnectedIndexed != null && IsGamepadConnectedIndexed(index) != 0;

    // 接続中のパッド数を返す
    internal static int ReadConnectedGamepadCount() =>
        GetConnectedGamepadCount != null ? GetConnectedGamepadCount() : 0;

    // Playerに割り当てたパッド番号を返す
    internal static int ReadPlayerGamepadIndex(int playerIndex) =>
        GetPlayerGamepadIndex != null ? GetPlayerGamepadIndex(playerIndex) : playerIndex;

    // Playerのキーボードとマウス割当を返す
    internal static bool ReadPlayerKeyboardMouseEnabled(int playerIndex) =>
        GetPlayerKeyboardMouseEnabled != null && GetPlayerKeyboardMouseEnabled(playerIndex) != 0;

    // Playerのゲーム入力許可状態を返す
    internal static bool ReadPlayerInputAvailable(int playerIndex) =>
        GetPlayerInputAvailable != null && GetPlayerInputAvailable(playerIndex) != 0;

    // Playerのパッド振動を開始する
    internal static uint PlayPlayerVibration(int playerIndex, float left, float right, float duration, float attack,
        float release) => NativeAPI.PlayPlayerVibration != null ?
        NativeAPI.PlayPlayerVibration(playerIndex, left, right, duration, attack, release) : 0;

    // Playerのパッド振動を停止する
    internal static void StopPlayerVibration(int playerIndex, uint handle) {

        if (NativeAPI.StopPlayerVibration != null) {
            NativeAPI.StopPlayerVibration(playerIndex, handle);
        }
    }

    // フレーム内の文字入力を読む
    internal static string ReadTextInput() {

        if (CopyTextInput == null) {
            return string.Empty;
        }
        return ManagedUTF8Transfer.ReadString(CopyTextInput);
    }

    // Playerのゲーム入力遮断状態を取得する
    internal static bool ReadUIBlocksGameplayInput(int playerIndex) {

        return GetUIBlocksGameplayInput != null && GetUIBlocksGameplayInput(playerIndex) != 0;
    }
}
