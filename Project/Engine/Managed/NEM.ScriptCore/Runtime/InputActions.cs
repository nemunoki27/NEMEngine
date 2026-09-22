using static NEMEngine.InputActionEvaluation;

namespace NEMEngine;

// gameplay hot path で使う compact action ID。name 解決は GetActionID で一度だけ行う。
public readonly struct InputActionID : IEquatable<InputActionID> {

    internal readonly int index;

    internal InputActionID(int index) {
        this.index = index;
    }

    public bool IsValid => index >= 0;

    public bool Equals(InputActionID other) => index == other.index;
    public override bool Equals(object? obj) => obj is InputActionID other && Equals(other);
    public override int GetHashCode() => index;
}

// rebind 用の単一入力の最小表現。
public enum InputDeviceKind {
    Keyboard,
    Mouse,
    Gamepad
}

public readonly struct InputBinding {

    public InputDeviceKind Device { get; }
    // KeyCode / MouseButton / GamepadButton のいずれかの値
    public int Code { get; }

    public InputBinding(InputDeviceKind device, int code) {
        Device = device;
        Code = code;
    }
}

// project-level の Input Action Map。起動時/明示 reload 時だけ JSON を parse し、
// 以降は compact index と runtime 配列で評価する（gameplay frame で JSON/file/string scan しない）。
// 設定: ProjectSettings/InputActions.json（default）。UserSettings/Runtime/InputActions.json（user override）。
public static class InputActions {

    // 現在のInputActionBindingを保存
    public static bool SaveBindings() {

        EnsureLoaded();
        return InputActionStorage.SaveBindings(actions);
    }

    // 保存済み定義と名前索引を更新
    private static void LoadFromDisk() {

        nameToIndex.Clear();
        actions = InputActionStorage.LoadFromDisk();
        for (int i = 0; i < actions.Length; ++i) {
            // duplicate name は最初の定義を優先（後勝ちにしない）
            if (!nameToIndex.ContainsKey(actions[i].name)) {
                nameToIndex[actions[i].name] = i;
            } else {
                NativeApplicationAPI.WriteLog(1, $"[InputActions] duplicate action name ignored: {actions[i].name}");
            }
        }

    }

    // UI が入力を消費しているフレームは gameplay クエリを抑止する層（editor / UI が設定する）。
    public static bool BlockGameplayInput { get; set; }

	private static bool IsGameplayInputBlocked => BlockGameplayInput || NativeAPI.ReadUIBlocksGameplayInput();

    private static readonly object gate = new();
    private static InputActionDefinition[] actions = Array.Empty<InputActionDefinition>();
    private static readonly Dictionary<string, int> nameToIndex = new(StringComparer.Ordinal);
    private static bool loaded;

    //========================================================================
    //	public API
    //========================================================================
    public static InputActionID GetActionID(string name) {
        EnsureLoaded();
        lock (gate) {
            return nameToIndex.TryGetValue(name, out int index) ? new InputActionID(index) : new InputActionID(-1);
        }
    }

    public static bool IsPressed(InputActionID action) {
        if (IsGameplayInputBlocked) {
            return false;
        }
        InputActionDefinition? def = Resolve(action);
        if (def == null) {
            return false;
        }
        foreach (InputActionBinding binding in def.bindings) {
            if (EvalButtonPhase(binding, InputButtonPhase.Held)) {
                return true;
            }
        }
        return false;
    }

    public static bool WasPressed(InputActionID action) {
        if (IsGameplayInputBlocked) {
            return false;
        }
        InputActionDefinition? def = Resolve(action);
        if (def == null) {
            return false;
        }
        foreach (InputActionBinding binding in def.bindings) {
            if (EvalButtonPhase(binding, InputButtonPhase.Down)) {
                return true;
            }
        }
        return false;
    }

    public static bool WasReleased(InputActionID action) {
        if (IsGameplayInputBlocked) {
            return false;
        }
        InputActionDefinition? def = Resolve(action);
        if (def == null) {
            return false;
        }
        foreach (InputActionBinding binding in def.bindings) {
            if (EvalButtonPhase(binding, InputButtonPhase.Up)) {
                return true;
            }
        }
        return false;
    }

    public static float ReadAxis(InputActionID action) {
        if (IsGameplayInputBlocked) {
            return 0.0f;
        }
        InputActionDefinition? def = Resolve(action);
        if (def == null) {
            return 0.0f;
        }
        float value = 0.0f;
        foreach (InputActionBinding binding in def.bindings) {
            float v = EvalAxis1D(binding);
            // 絶対値が大きい binding を優先（複数 binding の競合解決）
            if (System.MathF.Abs(v) > System.MathF.Abs(value)) {
                value = v;
            }
        }
        return Math.Clamp(value, -1.0f, 1.0f);
    }

    public static Vector2 ReadVector2(InputActionID action) {
        if (IsGameplayInputBlocked) {
            return Vector2.zero;
        }
        InputActionDefinition? def = Resolve(action);
        if (def == null) {
            return Vector2.zero;
        }
        Vector2 result = Vector2.zero;
        foreach (InputActionBinding binding in def.bindings) {
            Vector2 v = EvalVector2(binding);
            if (Vector2.Length(v) > Vector2.Length(result)) {
                result = v;
            }
        }
        return result;
    }

    // 単一 binding へ rebind する（compact ID 経由）。保存は SaveBindings で行う。
    public static bool Rebind(InputActionID action, InputBinding binding) {
        InputActionDefinition? def = Resolve(action);
        if (def == null) {
            return false;
        }
        lock (gate) {
            def.bindings.Clear();
            InputActionBinding b = new() { kind = InputActionBindingKind.Button, a = new ResolvedActionInput(binding.Device, binding.Code) };
            if (def.type == InputActionType.Axis1D) {
                b.kind = InputActionBindingKind.Axis1D;
            } else if (def.type == InputActionType.Vector2) {
                b.kind = InputActionBindingKind.Stick2D;
            }
            def.bindings.Add(b);
        }
        return true;
    }

    // user override ファイルへ atomic 保存する（project default は変更しない）。

    public static bool ReloadBindings() {
        lock (gate) {
            loaded = false;
        }
        EnsureLoaded();
        return actions.Length > 0;
    }

    // string convenience overload（初回に compact ID を解決して以降は cache）。hot path では ID 版を推奨。
    public static bool IsPressed(string action) => IsPressed(CachedID(action));
    public static bool WasPressed(string action) => WasPressed(CachedID(action));
    public static bool WasReleased(string action) => WasReleased(CachedID(action));
    public static float ReadAxis(string action) => ReadAxis(CachedID(action));
    public static Vector2 ReadVector2(string action) => ReadVector2(CachedID(action));

    //========================================================================
    //	internal
    //========================================================================
    private static readonly Dictionary<string, InputActionID> stringIDCache = new(StringComparer.Ordinal);

    private static InputActionID CachedID(string action) {
        lock (gate) {
            if (stringIDCache.TryGetValue(action, out InputActionID id)) {
                return id;
            }
        }
        InputActionID resolved = GetActionID(action);
        lock (gate) {
            stringIDCache[action] = resolved;
        }
        return resolved;
    }

    private static InputActionDefinition? Resolve(InputActionID action) {
        EnsureLoaded();
        InputActionDefinition[] snapshot = actions;
        return (action.index >= 0 && action.index < snapshot.Length) ? snapshot[action.index] : null;
    }

    //========================================================================
    //	load / parse / serialize
    //========================================================================
    private static void EnsureLoaded() {
        lock (gate) {
            if (loaded) {
                return;
            }
            loaded = true;
            stringIDCache.Clear();
            LoadFromDisk();
        }
    }

    // DLL reload 時に再 parse させる（古い delegate は持たないが、cache は破棄する）。
    internal static void ResetForReload() {
        lock (gate) {
            loaded = false;
            stringIDCache.Clear();
            BlockGameplayInput = false;
        }
    }
}
