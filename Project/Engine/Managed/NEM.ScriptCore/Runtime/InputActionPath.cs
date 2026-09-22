namespace NEMEngine;

// 入力パスと機器コードを相互変換する
internal static class InputActionPath {

    internal static ResolvedActionInput ParsePath(string path, string actionName) {
        int slash = path.IndexOf('/');
        if (slash <= 0) {
            return new ResolvedActionInput(InputDeviceKind.Keyboard, -1);
        }
        string device = path.Substring(0, slash);
        string code = path.Substring(slash + 1);
        switch (device) {
        case "Keyboard":
            return Enum.TryParse(code, out KeyCode key)
                ? new ResolvedActionInput(InputDeviceKind.Keyboard, (int)key)
                : Invalid(path, actionName);
        case "Mouse":
            if (Enum.TryParse(code, out MouseButton mb)) {
                return new ResolvedActionInput(InputDeviceKind.Mouse, (int)mb);
            }
            return int.TryParse(code, out int mi) ? new ResolvedActionInput(InputDeviceKind.Mouse, mi) : Invalid(path, actionName);
        case "Gamepad":
            if (Enum.TryParse(code, out GamepadButton gb)) {
                return new ResolvedActionInput(InputDeviceKind.Gamepad, (int)gb);
            }
            return Enum.TryParse(code, out GamepadAxis ga)
                ? new ResolvedActionInput(InputDeviceKind.Gamepad, (int)ga)
                : Invalid(path, actionName);
        default:
            return Invalid(path, actionName);
        }
    }

    internal static ResolvedActionInput Invalid(string path, string actionName) {
        NativeApplicationAPI.WriteLog(1, $"[InputActions] invalid binding path '{path}' in action '{actionName}'");
        return new ResolvedActionInput(InputDeviceKind.Keyboard, -1);
    }

    internal static string PathOf(ResolvedActionInput input) {
        if (!input.valid) {
            return string.Empty;
        }
        switch (input.device) {
        case InputDeviceKind.Keyboard:
            return "Keyboard/" + ((KeyCode)input.code);
        case InputDeviceKind.Mouse:
            return "Mouse/" + ((MouseButton)input.code);
        case InputDeviceKind.Gamepad:
            // gamepad は button / axis のどちらか。code 範囲で判別はせず button 名を優先（axis は別途 enum 名）
            return "Gamepad/" + ((GamepadButton)input.code);
        default:
            return string.Empty;
        }
    }
}
