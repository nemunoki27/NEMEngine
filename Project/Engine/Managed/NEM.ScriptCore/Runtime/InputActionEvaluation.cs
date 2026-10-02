namespace NEMEngine;

// 解決済みBindingから現在の入力値を評価する
internal static class InputActionEvaluation {

    internal static bool ResolvedButton(ResolvedActionInput input, InputButtonPhase phase, int playerIndex) {
        if (!input.valid) {
            return false;
        }
        switch (input.device) {
        case InputDeviceKind.Keyboard:
            return phase switch {
                InputButtonPhase.Held => PlayerInput.GetKey(playerIndex, (KeyCode)input.code),
                InputButtonPhase.Down => PlayerInput.GetKeyDown(playerIndex, (KeyCode)input.code),
                _ => PlayerInput.GetKeyUp(playerIndex, (KeyCode)input.code),
            };
        case InputDeviceKind.Mouse:
            return phase switch {
                InputButtonPhase.Held => PlayerInput.GetMouseButton(playerIndex, input.code),
                InputButtonPhase.Down => PlayerInput.GetMouseButtonDown(playerIndex, input.code),
                _ => PlayerInput.GetMouseButtonUp(playerIndex, input.code),
            };
        case InputDeviceKind.Gamepad:
            return phase switch {
                InputButtonPhase.Held => PlayerInput.GetGamepadButton(playerIndex, (GamepadButton)input.code),
                InputButtonPhase.Down => PlayerInput.GetGamepadButtonDown(playerIndex, (GamepadButton)input.code),
                _ => PlayerInput.GetGamepadButtonUp(playerIndex, (GamepadButton)input.code),
            };
        default:
            return false;
        }
    }

    internal static float ResolvedAxis(ResolvedActionInput input, int playerIndex) {
        if (!input.valid || input.device != InputDeviceKind.Gamepad) {
            return 0.0f;
        }
        return PlayerInput.GetGamepadAxis(playerIndex, (GamepadAxis)input.code);
    }

    internal static bool EvalButtonPhase(InputActionBinding binding, InputButtonPhase phase, int playerIndex) {
        switch (binding.kind) {
        case InputActionBindingKind.Button:
            return ResolvedButton(binding.a, phase, playerIndex);
        case InputActionBindingKind.Composite1D:
            return ResolvedButton(binding.a, phase, playerIndex) || ResolvedButton(binding.b, phase, playerIndex);
        case InputActionBindingKind.Composite2D:
            return ResolvedButton(binding.a, phase, playerIndex) || ResolvedButton(binding.b, phase, playerIndex)
                || ResolvedButton(binding.c, phase, playerIndex) || ResolvedButton(binding.d, phase, playerIndex);
        default:
            // axis 系は held のみ閾値で判定
            return phase == InputButtonPhase.Held && System.MathF.Abs(EvalAxis1D(binding, playerIndex)) > 0.5f;
        }
    }

    internal static float ApplyAxisShaping(float raw, InputActionBinding binding) {
        float v = raw;
        if (System.MathF.Abs(v) < binding.deadZone) {
            return 0.0f;
        }
        v *= binding.sensitivity;
        if (binding.invert) {
            v = -v;
        }
        return Mathf.Clamp(v, -1.0f, 1.0f);
    }

    internal static float EvalAxis1D(InputActionBinding binding, int playerIndex) {
        switch (binding.kind) {
        case InputActionBindingKind.Composite1D: {
            // a=positive, b=negative
            float v = (ResolvedButton(binding.a, InputButtonPhase.Held, playerIndex) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.b, InputButtonPhase.Held, playerIndex) ? 1.0f : 0.0f);
            return binding.invert ? -v : v;
        }
        case InputActionBindingKind.Axis1D:
            return ApplyAxisShaping(ResolvedAxis(binding.a, playerIndex), binding);
        case InputActionBindingKind.Button:
            return ResolvedButton(binding.a, InputButtonPhase.Held, playerIndex) ? (binding.invert ? -1.0f : 1.0f) : 0.0f;
        default:
            return 0.0f;
        }
    }

    internal static Vector2 EvalVector2(InputActionBinding binding, int playerIndex) {
        switch (binding.kind) {
        case InputActionBindingKind.Composite2D: {
            // a=up, b=down, c=left, d=right
            float x = (ResolvedButton(binding.d, InputButtonPhase.Held, playerIndex) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.c, InputButtonPhase.Held, playerIndex) ? 1.0f : 0.0f);
            float y = (ResolvedButton(binding.a, InputButtonPhase.Held, playerIndex) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.b, InputButtonPhase.Held, playerIndex) ? 1.0f : 0.0f);
            return new Vector2(x, y);
        }
        case InputActionBindingKind.Stick2D: {
            // a=axisX, b=axisY。radial dead zone を適用する
            Vector2 v = new(ResolvedAxis(binding.a, playerIndex), ResolvedAxis(binding.b, playerIndex));
            float mag = Vector2.Magnitude(v);
            if (mag < binding.deadZone || mag <= 0.0f) {
                return Vector2.zero;
            }
            // dead zone 境界から [0,1] へ再スケールし、sensitivity / invert を適用
            float scaled = Mathf.Clamp((mag - binding.deadZone) / (1.0f - binding.deadZone), 0.0f, 1.0f) * binding.sensitivity;
            Vector2 dir = v / mag;
            Vector2 result = dir * scaled;
            return binding.invert ? new Vector2(-result.x, -result.y) : result;
        }
        default:
            return Vector2.zero;
        }
    }
}
