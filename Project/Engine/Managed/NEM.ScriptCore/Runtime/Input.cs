namespace NEMEngine;

public enum KeyCode {

    None = 0,
    Escape = 0x01,
    Alpha1 = 0x02,
    Alpha2 = 0x03,
    Alpha3 = 0x04,
    Alpha4 = 0x05,
    Alpha5 = 0x06,
    Alpha6 = 0x07,
    Alpha7 = 0x08,
    Alpha8 = 0x09,
    Alpha9 = 0x0A,
    Alpha0 = 0x0B,
    Minus = 0x0C,
    Equals = 0x0D,
    Backspace = 0x0E,
    Tab = 0x0F,
    Q = 0x10,
    W = 0x11,
    E = 0x12,
    R = 0x13,
    T = 0x14,
    Y = 0x15,
    U = 0x16,
    I = 0x17,
    O = 0x18,
    P = 0x19,
    LeftBracket = 0x1A,
    RightBracket = 0x1B,
    Return = 0x1C,
    LeftControl = 0x1D,
    A = 0x1E,
    S = 0x1F,
    D = 0x20,
    F = 0x21,
    G = 0x22,
    H = 0x23,
    J = 0x24,
    K = 0x25,
    L = 0x26,
    Semicolon = 0x27,
    Apostrophe = 0x28,
    BackQuote = 0x29,
    LeftShift = 0x2A,
    Backslash = 0x2B,
    Z = 0x2C,
    X = 0x2D,
    C = 0x2E,
    V = 0x2F,
    B = 0x30,
    N = 0x31,
    M = 0x32,
    Comma = 0x33,
    Period = 0x34,
    Slash = 0x35,
    RightShift = 0x36,
    NumpadMultiply = 0x37,
    LeftAlt = 0x38,
    Space = 0x39,
    CapsLock = 0x3A,
    F1 = 0x3B,
    F2 = 0x3C,
    F3 = 0x3D,
    F4 = 0x3E,
    F5 = 0x3F,
    F6 = 0x40,
    F7 = 0x41,
    F8 = 0x42,
    F9 = 0x43,
    F10 = 0x44,
    NumLock = 0x45,
    ScrollLock = 0x46,
    Numpad7 = 0x47,
    Numpad8 = 0x48,
    Numpad9 = 0x49,
    NumpadSubtract = 0x4A,
    Numpad4 = 0x4B,
    Numpad5 = 0x4C,
    Numpad6 = 0x4D,
    NumpadAdd = 0x4E,
    Numpad1 = 0x4F,
    Numpad2 = 0x50,
    Numpad3 = 0x51,
    Numpad0 = 0x52,
    NumpadDecimal = 0x53,
    F11 = 0x57,
    F12 = 0x58,
    NumpadEnter = 0x9C,
    RightControl = 0x9D,
    NumpadDivide = 0xB5,
    RightAlt = 0xB8,
    Home = 0xC7,
    UpArrow = 0xC8,
    PageUp = 0xC9,
    LeftArrow = 0xCB,
    RightArrow = 0xCD,
    End = 0xCF,
    DownArrow = 0xD0,
    PageDown = 0xD1,
    Insert = 0xD2,
    Delete = 0xD3,
    LeftWindows = 0xDB,
    RightWindows = 0xDC,
    Applications = 0xDD
}

public enum MouseButton {

    Left = 0,
    Right = 1,
    Middle = 2
}

public enum GamepadButton {

    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    Start = 4,
    Back = 5,
    LeftThumb = 6,
    RightThumb = 7,
    LeftShoulder = 8,
    RightShoulder = 9,
    LeftTrigger = 10,
    RightTrigger = 11,
    A = 12,
    B = 13,
    X = 14,
    Y = 15
}

// gamepad のアナログ軸。値の index は native の GamepadAxisByIndex と一致させる。
public enum GamepadAxis {

    LeftStickX = 0,
    LeftStickY = 1,
    RightStickX = 2,
    RightStickY = 3,
    LeftTrigger = 4,
    RightTrigger = 5
}

// 入力デバイス種別、C++ InputTypeと値を一致させる
public enum InputType {

    Keyboard,
    GamePad,
}

public static class Input {

    public static Vector2 mousePosition => NativeInputAPI.ReadMousePosition();
    public static Vector2 mouseDelta => NativeInputAPI.ReadMouseDelta();
    public static float mouseWheel => NativeInputAPI.ReadMouseWheel();
    public static bool isGamepadConnected => NativeInputAPI.ReadIsGamepadConnected();

    // 最後に操作された入力デバイスタイプ
    public static InputType inputType => (InputType)NativeInputAPI.ReadInputType();
    // マウス移動範囲制御のON/OFF
    public static bool mouseRangeControl {
        get => NativeInputAPI.ReadMouseRangeControl();
        set => NativeInputAPI.WriteMouseRangeControl(value);
    }
    public static Vector2 leftStick => NativeInputAPI.ReadLeftStick();
    public static Vector2 rightStick => NativeInputAPI.ReadRightStick();
    public static float leftTrigger => NativeInputAPI.ReadLeftTrigger();
    public static float rightTrigger => NativeInputAPI.ReadRightTrigger();

    public static bool GetKey(KeyCode key) {
        return NativeInputAPI.ReadKey((int)key);
    }

    public static bool GetKeyDown(KeyCode key) {
        return NativeInputAPI.ReadKeyDown((int)key);
    }

    public static bool GetKeyUp(KeyCode key) {
        return NativeInputAPI.ReadKeyUp((int)key);
    }

    public static bool GetMouseButton(MouseButton button) {
        return NativeInputAPI.ReadMouseButton((int)button);
    }

    public static bool GetMouseButtonDown(MouseButton button) {
        return NativeInputAPI.ReadMouseButtonDown((int)button);
    }

    public static bool GetMouseButtonUp(MouseButton button) {
        return NativeInputAPI.ReadMouseButtonUp((int)button);
    }

    // int overload（0=Left, 1=Right, 2=Middle）
    public static bool GetMouseButton(int button) => NativeInputAPI.ReadMouseButton(button);
    public static bool GetMouseButtonDown(int button) => NativeInputAPI.ReadMouseButtonDown(button);
    public static bool GetMouseButtonUp(int button) => NativeInputAPI.ReadMouseButtonUp(button);

    // 単一 gamepad（index 0）向け convenience。
    public static bool GetGamepadButton(GamepadButton button) => NativeInputAPI.ReadGamepadButton(0, (int)button);
    public static bool GetGamepadButtonDown(GamepadButton button) => NativeInputAPI.ReadGamepadButtonDown(0, (int)button);
    public static bool GetGamepadButtonUp(GamepadButton button) => NativeInputAPI.ReadGamepadButtonUp(0, (int)button);

    //--------- 多 gamepad（最大4台）/ text / focus ----------------------------

    public static bool IsGamepadConnected(int index) => NativeInputAPI.ReadGamepadConnected(index);
    public static int ConnectedGamepadCount => NativeInputAPI.ReadConnectedGamepadCount();

    public static bool GetGamepadButton(int index, GamepadButton button) => NativeInputAPI.ReadGamepadButton(index, (int)button);
    public static bool GetGamepadButtonDown(int index, GamepadButton button) => NativeInputAPI.ReadGamepadButtonDown(index, (int)button);
    public static bool GetGamepadButtonUp(int index, GamepadButton button) => NativeInputAPI.ReadGamepadButtonUp(index, (int)button);

    // dead zone 未適用の raw 軸値（stick は [-1,1]、trigger は [0,1]）。dead zone は InputActions 側で適用する。
    public static float GetGamepadAxis(int index, GamepadAxis axis) => NativeInputAPI.ReadGamepadAxis(index, (int)axis);

    // ウィンドウがフォーカスを持っているか
    public static bool HasFocus => NativeApplicationAPI.ReadHasFocus();

    // このフレームに入力された文字列（frame-local。確定文字のみ）
    public static string TextInput => NativeInputAPI.ReadTextInput();
}
