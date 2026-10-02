namespace NEMEngine;

//============================================================================
//	PlayerInput class
//	Playerへ割り当てたデバイスからゲーム入力を取得する
//============================================================================
public static class PlayerInput {

    public const int MaxPlayers = 4;

    public static bool IsAvailable(int playerIndex) =>
        IsValidPlayer(playerIndex) && NativeInputAPI.ReadPlayerInputAvailable(playerIndex);

    public static int GetGamepadIndex(int playerIndex) => IsValidPlayer(playerIndex) ?
        NativeInputAPI.ReadPlayerGamepadIndex(playerIndex) : -1;

    public static bool GetKey(int playerIndex, KeyCode key) =>
        CanReadKeyboardMouse(playerIndex) && Input.GetKey(key);

    public static bool GetKeyDown(int playerIndex, KeyCode key) =>
        CanReadKeyboardMouse(playerIndex) && Input.GetKeyDown(key);

    public static bool GetKeyUp(int playerIndex, KeyCode key) =>
        CanReadKeyboardMouse(playerIndex) && Input.GetKeyUp(key);

    public static bool GetMouseButton(int playerIndex, int button) =>
        CanReadKeyboardMouse(playerIndex) && Input.GetMouseButton(button);

    public static bool GetMouseButtonDown(int playerIndex, int button) =>
        CanReadKeyboardMouse(playerIndex) && Input.GetMouseButtonDown(button);

    public static bool GetMouseButtonUp(int playerIndex, int button) =>
        CanReadKeyboardMouse(playerIndex) && Input.GetMouseButtonUp(button);

    public static bool GetGamepadButton(int playerIndex, GamepadButton button) {

        int gamepadIndex = GetReadableGamepadIndex(playerIndex);
        return gamepadIndex >= 0 && Input.GetGamepadButton(gamepadIndex, button);
    }

    public static bool GetGamepadButtonDown(int playerIndex, GamepadButton button) {

        int gamepadIndex = GetReadableGamepadIndex(playerIndex);
        return gamepadIndex >= 0 && Input.GetGamepadButtonDown(gamepadIndex, button);
    }

    public static bool GetGamepadButtonUp(int playerIndex, GamepadButton button) {

        int gamepadIndex = GetReadableGamepadIndex(playerIndex);
        return gamepadIndex >= 0 && Input.GetGamepadButtonUp(gamepadIndex, button);
    }

    public static float GetGamepadAxis(int playerIndex, GamepadAxis axis) {

        int gamepadIndex = GetReadableGamepadIndex(playerIndex);
        return gamepadIndex >= 0 ? Input.GetGamepadAxis(gamepadIndex, axis) : 0.0f;
    }

    public static uint PlayVibration(int playerIndex, float left, float right,
        float duration, float attack = 0.0f, float release = 0.0f) {

        return IsAvailable(playerIndex) ? NativeInputAPI.PlayPlayerVibration(
            playerIndex, left, right, duration, attack, release) : 0;
    }

    public static void StopVibration(int playerIndex, uint handle) {

        if (IsValidPlayer(playerIndex)) {
            NativeInputAPI.StopPlayerVibration(playerIndex, handle);
        }
    }

    private static bool CanReadKeyboardMouse(int playerIndex) => IsAvailable(playerIndex) &&
        NativeInputAPI.ReadPlayerKeyboardMouseEnabled(playerIndex);

    private static int GetReadableGamepadIndex(int playerIndex) =>
        IsAvailable(playerIndex) ? GetGamepadIndex(playerIndex) : -1;

    private static bool IsValidPlayer(int playerIndex) =>
        playerIndex >= 0 && playerIndex < MaxPlayers;
}
