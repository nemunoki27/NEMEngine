namespace NEMEngine;

// 解決済みBindingから現在の入力値を評価する
internal static class InputActionEvaluation {

    internal static bool ResolvedButton(ResolvedActionInput input, InputButtonPhase phase) {
        if (!input.valid) {
            return false;
        }
        switch (input.device) {
        case InputDeviceKind.Keyboard:
            return phase switch {
                InputButtonPhase.Held => Input.GetKey((KeyCode)input.code),
                InputButtonPhase.Down => Input.GetKeyDown((KeyCode)input.code),
                _ => Input.GetKeyUp((KeyCode)input.code),
            };
        case InputDeviceKind.Mouse:
            return phase switch {
                InputButtonPhase.Held => Input.GetMouseButton(input.code),
                InputButtonPhase.Down => Input.GetMouseButtonDown(input.code),
                _ => Input.GetMouseButtonUp(input.code),
            };
        case InputDeviceKind.Gamepad:
            return phase switch {
                InputButtonPhase.Held => Input.GetGamepadButton(0, (GamepadButton)input.code),
                InputButtonPhase.Down => Input.GetGamepadButtonDown(0, (GamepadButton)input.code),
                _ => Input.GetGamepadButtonUp(0, (GamepadButton)input.code),
            };
        default:
            return false;
        }
    }

    internal static float ResolvedAxis(ResolvedActionInput input) {
        if (!input.valid || input.device != InputDeviceKind.Gamepad) {
            return 0.0f;
        }
        return Input.GetGamepadAxis(0, (GamepadAxis)input.code);
    }

    internal static bool EvalButtonPhase(InputActionBinding binding, InputButtonPhase phase) {
        switch (binding.kind) {
        case InputActionBindingKind.Button:
            return ResolvedButton(binding.a, phase);
        case InputActionBindingKind.Composite1D:
            return ResolvedButton(binding.a, phase) || ResolvedButton(binding.b, phase);
        case InputActionBindingKind.Composite2D:
            return ResolvedButton(binding.a, phase) || ResolvedButton(binding.b, phase)
                || ResolvedButton(binding.c, phase) || ResolvedButton(binding.d, phase);
        default:
            // axis 系は held のみ閾値で判定
            return phase == InputButtonPhase.Held && System.MathF.Abs(EvalAxis1D(binding)) > 0.5f;
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

    internal static float EvalAxis1D(InputActionBinding binding) {
        switch (binding.kind) {
        case InputActionBindingKind.Composite1D: {
            // a=positive, b=negative
            float v = (ResolvedButton(binding.a, InputButtonPhase.Held) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.b, InputButtonPhase.Held) ? 1.0f : 0.0f);
            return binding.invert ? -v : v;
        }
        case InputActionBindingKind.Axis1D:
            return ApplyAxisShaping(ResolvedAxis(binding.a), binding);
        case InputActionBindingKind.Button:
            return ResolvedButton(binding.a, InputButtonPhase.Held) ? (binding.invert ? -1.0f : 1.0f) : 0.0f;
        default:
            return 0.0f;
        }
    }

    internal static Vector2 EvalVector2(InputActionBinding binding) {
        switch (binding.kind) {
        case InputActionBindingKind.Composite2D: {
            // a=up, b=down, c=left, d=right
            float x = (ResolvedButton(binding.d, InputButtonPhase.Held) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.c, InputButtonPhase.Held) ? 1.0f : 0.0f);
            float y = (ResolvedButton(binding.a, InputButtonPhase.Held) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.b, InputButtonPhase.Held) ? 1.0f : 0.0f);
            return new Vector2(x, y);
        }
        case InputActionBindingKind.Stick2D: {
            // a=axisX, b=axisY。radial dead zone を適用する
            Vector2 v = new(ResolvedAxis(binding.a), ResolvedAxis(binding.b));
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
