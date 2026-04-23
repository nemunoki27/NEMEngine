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
    A = 0x1E,
    S = 0x1F,
    D = 0x20,
    F = 0x21,
    G = 0x22,
    H = 0x23,
    J = 0x24,
    K = 0x25,
    L = 0x26,
    Return = 0x1C,
    LeftControl = 0x1D,
    LeftShift = 0x2A,
    LeftAlt = 0x38,
    Space = 0x39,
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
    F11 = 0x57,
    F12 = 0x58,
    LeftArrow = 0xCB,
    RightArrow = 0xCD,
    UpArrow = 0xC8,
    DownArrow = 0xD0
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

public static class Input {

    public static Vector2 mousePosition => NativeApi.ReadMousePosition();
    public static Vector2 mouseDelta => NativeApi.ReadMouseDelta();
    public static float mouseWheel => NativeApi.ReadMouseWheel();
    public static bool isGamepadConnected => NativeApi.ReadIsGamepadConnected();
    public static Vector2 leftStick => NativeApi.ReadLeftStick();
    public static Vector2 rightStick => NativeApi.ReadRightStick();
    public static float leftTrigger => NativeApi.ReadLeftTrigger();
    public static float rightTrigger => NativeApi.ReadRightTrigger();

    public static bool GetKey(KeyCode key) {
        return NativeApi.ReadKey((int)key);
    }

    public static bool GetKeyDown(KeyCode key) {
        return NativeApi.ReadKeyDown((int)key);
    }

    public static bool GetKeyUp(KeyCode key) {
        return NativeApi.ReadKeyUp((int)key);
    }

    public static bool GetMouseButton(MouseButton button) {
        return NativeApi.ReadMouseButton((int)button);
    }

    public static bool GetMouseButtonDown(MouseButton button) {
        return NativeApi.ReadMouseButtonDown((int)button);
    }

    public static bool GetMouseButtonUp(MouseButton button) {
        return NativeApi.ReadMouseButtonUp((int)button);
    }

    public static bool GetGamepadButton(GamepadButton button) {
        return NativeApi.ReadGamepadButton((int)button);
    }

    public static bool GetGamepadButtonDown(GamepadButton button) {
        return NativeApi.ReadGamepadButtonDown((int)button);
    }
}
