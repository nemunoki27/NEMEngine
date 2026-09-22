namespace NEMEngine;

internal enum InputActionType { Button, Axis1D, Vector2 }

internal enum InputActionBindingKind { Button, Composite1D, Axis1D, Composite2D, Stick2D }

internal readonly struct ResolvedActionInput {
    public readonly InputDeviceKind device;
    public readonly int code;
    public ResolvedActionInput(InputDeviceKind device, int code) { this.device = device; this.code = code; }
    public bool valid => code >= 0;
}

internal sealed class InputActionBinding {
    public InputActionBindingKind kind;
    public ResolvedActionInput a;   // Button / Composite: up / Composite1D: positive / axis: x
    public ResolvedActionInput b;   // Composite: down / Composite1D: negative / axis: y
    public ResolvedActionInput c;   // Composite2D: left
    public ResolvedActionInput d;   // Composite2D: right
    public float deadZone = 0.0f;
    public float sensitivity = 1.0f;
    public bool invert;
}

internal sealed class InputActionDefinition {
    public string name = string.Empty;
    public InputActionType type;
    public readonly List<InputActionBinding> bindings = new();
}

internal enum InputButtonPhase { Held, Down, Up }
