using System.Text;
using System.Text.Json;

namespace NEMEngine;

// gameplay hot path で使う compact action ID。name 解決は GetActionId で一度だけ行う。
public readonly struct InputActionId : IEquatable<InputActionId> {

    internal readonly int index;

    internal InputActionId(int index) {
        this.index = index;
    }

    public bool IsValid => index >= 0;

    public bool Equals(InputActionId other) => index == other.index;
    public override bool Equals(object? obj) => obj is InputActionId other && Equals(other);
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
// 設定: ProjectSettings/InputActions.json（default）。ProjectSettings/InputActions.user.json（user override）。
public static class InputActions {

    private const string DefaultFileName = "InputActions.json";
    private const string UserFileName = "InputActions.user.json";
    private const int SchemaVersion = 1;

    // UI が入力を消費しているフレームは gameplay クエリを抑止する層（editor / UI が設定する）。
    public static bool BlockGameplayInput { get; set; }

	private static bool IsGameplayInputBlocked => BlockGameplayInput || NativeApi.ReadUIBlocksGameplayInput();

    private enum ActionType { Button, Axis1D, Vector2 }
    private enum BindingKind { Button, Composite1D, Axis1D, Composite2D, Stick2D }

    private readonly struct ResolvedInput {
        public readonly InputDeviceKind device;
        public readonly int code;
        public ResolvedInput(InputDeviceKind device, int code) { this.device = device; this.code = code; }
        public bool valid => code >= 0;
    }

    private sealed class Binding {
        public BindingKind kind;
        public ResolvedInput a;   // Button / Composite: up / Composite1D: positive / axis: x
        public ResolvedInput b;   // Composite: down / Composite1D: negative / axis: y
        public ResolvedInput c;   // Composite2D: left
        public ResolvedInput d;   // Composite2D: right
        public float deadZone = 0.0f;
        public float sensitivity = 1.0f;
        public bool invert;
    }

    private sealed class ActionDef {
        public string name = string.Empty;
        public ActionType type;
        public readonly List<Binding> bindings = new();
    }

    private static readonly object gate = new();
    private static ActionDef[] actions = Array.Empty<ActionDef>();
    private static readonly Dictionary<string, int> nameToIndex = new(StringComparer.Ordinal);
    private static bool loaded;

    //========================================================================
    //	public API
    //========================================================================
    public static InputActionId GetActionId(string name) {
        EnsureLoaded();
        lock (gate) {
            return nameToIndex.TryGetValue(name, out int index) ? new InputActionId(index) : new InputActionId(-1);
        }
    }

    public static bool IsPressed(InputActionId action) {
        if (IsGameplayInputBlocked) {
            return false;
        }
        ActionDef? def = Resolve(action);
        if (def == null) {
            return false;
        }
        foreach (Binding binding in def.bindings) {
            if (EvalButtonPhase(binding, ButtonPhase.Held)) {
                return true;
            }
        }
        return false;
    }

    public static bool WasPressed(InputActionId action) {
        if (IsGameplayInputBlocked) {
            return false;
        }
        ActionDef? def = Resolve(action);
        if (def == null) {
            return false;
        }
        foreach (Binding binding in def.bindings) {
            if (EvalButtonPhase(binding, ButtonPhase.Down)) {
                return true;
            }
        }
        return false;
    }

    public static bool WasReleased(InputActionId action) {
        if (IsGameplayInputBlocked) {
            return false;
        }
        ActionDef? def = Resolve(action);
        if (def == null) {
            return false;
        }
        foreach (Binding binding in def.bindings) {
            if (EvalButtonPhase(binding, ButtonPhase.Up)) {
                return true;
            }
        }
        return false;
    }

    public static float ReadAxis(InputActionId action) {
        if (IsGameplayInputBlocked) {
            return 0.0f;
        }
        ActionDef? def = Resolve(action);
        if (def == null) {
            return 0.0f;
        }
        float value = 0.0f;
        foreach (Binding binding in def.bindings) {
            float v = EvalAxis1D(binding);
            // 絶対値が大きい binding を優先（複数 binding の競合解決）
            if (System.MathF.Abs(v) > System.MathF.Abs(value)) {
                value = v;
            }
        }
        return Math.Clamp(value, -1.0f, 1.0f);
    }

    public static Vector2 ReadVector2(InputActionId action) {
        if (IsGameplayInputBlocked) {
            return Vector2.zero;
        }
        ActionDef? def = Resolve(action);
        if (def == null) {
            return Vector2.zero;
        }
        Vector2 result = Vector2.zero;
        foreach (Binding binding in def.bindings) {
            Vector2 v = EvalVector2(binding);
            if (Vector2.Length(v) > Vector2.Length(result)) {
                result = v;
            }
        }
        return result;
    }

    // 単一 binding へ rebind する（compact ID 経由）。保存は SaveBindings で行う。
    public static bool Rebind(InputActionId action, InputBinding binding) {
        ActionDef? def = Resolve(action);
        if (def == null) {
            return false;
        }
        lock (gate) {
            def.bindings.Clear();
            Binding b = new() { kind = BindingKind.Button, a = new ResolvedInput(binding.Device, binding.Code) };
            if (def.type == ActionType.Axis1D) {
                b.kind = BindingKind.Axis1D;
            } else if (def.type == ActionType.Vector2) {
                b.kind = BindingKind.Stick2D;
            }
            def.bindings.Add(b);
        }
        return true;
    }

    // user override ファイルへ atomic 保存する（project default は変更しない）。
    public static bool SaveBindings() {
        EnsureLoaded();
        string? dir = SettingsDirectory();
        if (dir == null) {
            return false;
        }
        try {
            string json = Serialize();
            string target = Path.Combine(dir, UserFileName);
            string temp = target + ".tmp";
            File.WriteAllText(temp, json, new UTF8Encoding(false));
            // temp -> 本ファイルへ原子的に置換（既存があれば上書き）
            if (File.Exists(target)) {
                File.Replace(temp, target, null);
            } else {
                File.Move(temp, target);
            }
            return true;
        }
        catch (Exception e) {
            NativeApi.WriteLog(2, $"[InputActions] SaveBindings failed\n{e}");
            return false;
        }
    }

    public static bool ReloadBindings() {
        lock (gate) {
            loaded = false;
        }
        EnsureLoaded();
        return actions.Length > 0;
    }

    // string convenience overload（初回に compact ID を解決して以降は cache）。hot path では ID 版を推奨。
    public static bool IsPressed(string action) => IsPressed(CachedId(action));
    public static bool WasPressed(string action) => WasPressed(CachedId(action));
    public static bool WasReleased(string action) => WasReleased(CachedId(action));
    public static float ReadAxis(string action) => ReadAxis(CachedId(action));
    public static Vector2 ReadVector2(string action) => ReadVector2(CachedId(action));

    //========================================================================
    //	internal
    //========================================================================
    private static readonly Dictionary<string, InputActionId> stringIdCache = new(StringComparer.Ordinal);

    private static InputActionId CachedId(string action) {
        lock (gate) {
            if (stringIdCache.TryGetValue(action, out InputActionId id)) {
                return id;
            }
        }
        InputActionId resolved = GetActionId(action);
        lock (gate) {
            stringIdCache[action] = resolved;
        }
        return resolved;
    }

    private static ActionDef? Resolve(InputActionId action) {
        EnsureLoaded();
        ActionDef[] snapshot = actions;
        return (action.index >= 0 && action.index < snapshot.Length) ? snapshot[action.index] : null;
    }

    private enum ButtonPhase { Held, Down, Up }

    private static bool ResolvedButton(ResolvedInput input, ButtonPhase phase) {
        if (!input.valid) {
            return false;
        }
        switch (input.device) {
        case InputDeviceKind.Keyboard:
            return phase switch {
                ButtonPhase.Held => Input.GetKey((KeyCode)input.code),
                ButtonPhase.Down => Input.GetKeyDown((KeyCode)input.code),
                _ => Input.GetKeyUp((KeyCode)input.code),
            };
        case InputDeviceKind.Mouse:
            return phase switch {
                ButtonPhase.Held => Input.GetMouseButton(input.code),
                ButtonPhase.Down => Input.GetMouseButtonDown(input.code),
                _ => Input.GetMouseButtonUp(input.code),
            };
        case InputDeviceKind.Gamepad:
            return phase switch {
                ButtonPhase.Held => Input.GetGamepadButton(0, (GamepadButton)input.code),
                ButtonPhase.Down => Input.GetGamepadButtonDown(0, (GamepadButton)input.code),
                _ => Input.GetGamepadButtonUp(0, (GamepadButton)input.code),
            };
        default:
            return false;
        }
    }

    // gamepad axis device の raw 値（stick/trigger）。keyboard/mouse は button 扱いのため 0。
    private static float ResolvedAxis(ResolvedInput input) {
        if (!input.valid || input.device != InputDeviceKind.Gamepad) {
            return 0.0f;
        }
        return Input.GetGamepadAxis(0, (GamepadAxis)input.code);
    }

    private static bool EvalButtonPhase(Binding binding, ButtonPhase phase) {
        switch (binding.kind) {
        case BindingKind.Button:
            return ResolvedButton(binding.a, phase);
        case BindingKind.Composite1D:
            return ResolvedButton(binding.a, phase) || ResolvedButton(binding.b, phase);
        case BindingKind.Composite2D:
            return ResolvedButton(binding.a, phase) || ResolvedButton(binding.b, phase)
                || ResolvedButton(binding.c, phase) || ResolvedButton(binding.d, phase);
        default:
            // axis 系は held のみ閾値で判定
            return phase == ButtonPhase.Held && System.MathF.Abs(EvalAxis1D(binding)) > 0.5f;
        }
    }

    private static float ApplyAxisShaping(float raw, Binding binding) {
        float v = raw;
        if (System.MathF.Abs(v) < binding.deadZone) {
            return 0.0f;
        }
        v *= binding.sensitivity;
        if (binding.invert) {
            v = -v;
        }
        return Math.Clamp(v, -1.0f, 1.0f);
    }

    private static float EvalAxis1D(Binding binding) {
        switch (binding.kind) {
        case BindingKind.Composite1D: {
            // a=positive, b=negative
            float v = (ResolvedButton(binding.a, ButtonPhase.Held) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.b, ButtonPhase.Held) ? 1.0f : 0.0f);
            return binding.invert ? -v : v;
        }
        case BindingKind.Axis1D:
            return ApplyAxisShaping(ResolvedAxis(binding.a), binding);
        case BindingKind.Button:
            return ResolvedButton(binding.a, ButtonPhase.Held) ? (binding.invert ? -1.0f : 1.0f) : 0.0f;
        default:
            return 0.0f;
        }
    }

    private static Vector2 EvalVector2(Binding binding) {
        switch (binding.kind) {
        case BindingKind.Composite2D: {
            // a=up, b=down, c=left, d=right
            float x = (ResolvedButton(binding.d, ButtonPhase.Held) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.c, ButtonPhase.Held) ? 1.0f : 0.0f);
            float y = (ResolvedButton(binding.a, ButtonPhase.Held) ? 1.0f : 0.0f)
                - (ResolvedButton(binding.b, ButtonPhase.Held) ? 1.0f : 0.0f);
            return new Vector2(x, y);
        }
        case BindingKind.Stick2D: {
            // a=axisX, b=axisY。radial dead zone を適用する
            Vector2 v = new(ResolvedAxis(binding.a), ResolvedAxis(binding.b));
            float mag = Vector2.Length(v);
            if (mag < binding.deadZone || mag <= 0.0f) {
                return Vector2.zero;
            }
            // dead zone 境界から [0,1] へ再スケールし、sensitivity / invert を適用
            float scaled = Math.Clamp((mag - binding.deadZone) / (1.0f - binding.deadZone), 0.0f, 1.0f) * binding.sensitivity;
            Vector2 dir = v / mag;
            Vector2 result = dir * scaled;
            return binding.invert ? new Vector2(-result.x, -result.y) : result;
        }
        default:
            return Vector2.zero;
        }
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
            stringIdCache.Clear();
            LoadFromDisk();
        }
    }

    private static string? SettingsDirectory() {
        string root = NativeApi.ReadProjectRoot();
        if (string.IsNullOrEmpty(root)) {
            return null;
        }
        return Path.Combine(root, "ProjectSettings");
    }

    private static void LoadFromDisk() {
        var parsed = new List<ActionDef>();
        nameToIndex.Clear();
        string? dir = SettingsDirectory();
        if (dir != null) {
            // user override を優先し、無ければ project default を読む
            string userPath = Path.Combine(dir, UserFileName);
            string defaultPath = Path.Combine(dir, DefaultFileName);
            string? path = File.Exists(userPath) ? userPath : (File.Exists(defaultPath) ? defaultPath : null);
            if (path != null) {
                TryParseFile(path, parsed);
            }
        }
        actions = parsed.ToArray();
        for (int i = 0; i < actions.Length; ++i) {
            // duplicate name は最初の定義を優先（後勝ちにしない）
            if (!nameToIndex.ContainsKey(actions[i].name)) {
                nameToIndex[actions[i].name] = i;
            } else {
                NativeApi.WriteLog(1, $"[InputActions] duplicate action name ignored: {actions[i].name}");
            }
        }
    }

    private static void TryParseFile(string path, List<ActionDef> outActions) {
        try {
            using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
            JsonElement root = doc.RootElement;
            if (!root.TryGetProperty("actions", out JsonElement actionsEl) || actionsEl.ValueKind != JsonValueKind.Array) {
                return;
            }
            foreach (JsonElement a in actionsEl.EnumerateArray()) {
                ActionDef def = new() {
                    name = a.TryGetProperty("name", out JsonElement n) ? (n.GetString() ?? string.Empty) : string.Empty,
                    type = ParseActionType(a.TryGetProperty("type", out JsonElement t) ? t.GetString() : null),
                };
                if (string.IsNullOrEmpty(def.name)) {
                    continue;
                }
                if (a.TryGetProperty("bindings", out JsonElement bs) && bs.ValueKind == JsonValueKind.Array) {
                    foreach (JsonElement b in bs.EnumerateArray()) {
                        Binding? binding = ParseBinding(b, def.name);
                        if (binding != null) {
                            def.bindings.Add(binding);
                        }
                    }
                }
                outActions.Add(def);
            }
        }
        catch (Exception e) {
            // parse 失敗は crash させず、空のまま続行（last-known-good を壊さない）
            NativeApi.WriteLog(2, $"[InputActions] parse failed: {path}\n{e}");
        }
    }

    private static ActionType ParseActionType(string? s) {
        return s switch {
            "Axis1D" => ActionType.Axis1D,
            "Vector2" => ActionType.Vector2,
            _ => ActionType.Button,
        };
    }

    private static Binding? ParseBinding(JsonElement b, string actionName) {
        string kind = b.TryGetProperty("kind", out JsonElement k) ? (k.GetString() ?? "Button") : "Button";
        Binding binding = new();
        binding.deadZone = b.TryGetProperty("deadZone", out JsonElement dz) ? (float)dz.GetDouble() : 0.0f;
        binding.sensitivity = b.TryGetProperty("sensitivity", out JsonElement se) ? (float)se.GetDouble() : 1.0f;
        binding.invert = b.TryGetProperty("invert", out JsonElement iv) && iv.GetBoolean();

        switch (kind) {
        case "Button":
            binding.kind = BindingKind.Button;
            binding.a = ParsePath(Str(b, "path"), actionName);
            break;
        case "Axis1D":
            if (b.TryGetProperty("axis", out _)) {
                binding.kind = BindingKind.Axis1D;
                binding.a = ParsePath(Str(b, "axis"), actionName);
            } else {
                binding.kind = BindingKind.Composite1D;
                binding.a = ParsePath(Str(b, "positive"), actionName);
                binding.b = ParsePath(Str(b, "negative"), actionName);
            }
            break;
        case "Composite2D":
            binding.kind = BindingKind.Composite2D;
            binding.a = ParsePath(Str(b, "up"), actionName);
            binding.b = ParsePath(Str(b, "down"), actionName);
            binding.c = ParsePath(Str(b, "left"), actionName);
            binding.d = ParsePath(Str(b, "right"), actionName);
            break;
        case "Stick2D":
            binding.kind = BindingKind.Stick2D;
            binding.a = ParsePath(Str(b, "x"), actionName);
            binding.b = ParsePath(Str(b, "y"), actionName);
            break;
        default:
            NativeApi.WriteLog(1, $"[InputActions] unknown binding kind '{kind}' in action '{actionName}'");
            return null;
        }
        return binding;
    }

    private static string Str(JsonElement e, string key) {
        return e.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? (v.GetString() ?? string.Empty) : string.Empty;
    }

    // "Keyboard/W" / "Mouse/Left" / "Gamepad/A" / "Gamepad/LeftStickX" を (device, code) へ解決する（load 時のみ）。
    private static ResolvedInput ParsePath(string path, string actionName) {
        int slash = path.IndexOf('/');
        if (slash <= 0) {
            return new ResolvedInput(InputDeviceKind.Keyboard, -1);
        }
        string device = path.Substring(0, slash);
        string code = path.Substring(slash + 1);
        switch (device) {
        case "Keyboard":
            return Enum.TryParse(code, out KeyCode key)
                ? new ResolvedInput(InputDeviceKind.Keyboard, (int)key)
                : Invalid(path, actionName);
        case "Mouse":
            if (Enum.TryParse(code, out MouseButton mb)) {
                return new ResolvedInput(InputDeviceKind.Mouse, (int)mb);
            }
            return int.TryParse(code, out int mi) ? new ResolvedInput(InputDeviceKind.Mouse, mi) : Invalid(path, actionName);
        case "Gamepad":
            if (Enum.TryParse(code, out GamepadButton gb)) {
                return new ResolvedInput(InputDeviceKind.Gamepad, (int)gb);
            }
            return Enum.TryParse(code, out GamepadAxis ga)
                ? new ResolvedInput(InputDeviceKind.Gamepad, (int)ga)
                : Invalid(path, actionName);
        default:
            return Invalid(path, actionName);
        }
    }

    private static ResolvedInput Invalid(string path, string actionName) {
        NativeApi.WriteLog(1, $"[InputActions] invalid binding path '{path}' in action '{actionName}'");
        return new ResolvedInput(InputDeviceKind.Keyboard, -1);
    }

    private static string Serialize() {
        // 現在の action 定義を JSON へ書き出す（user override 用）。device/code を path 文字列へ戻す。
        var sb = new StringBuilder();
        sb.Append("{\n  \"schemaVersion\": ").Append(SchemaVersion).Append(",\n  \"actions\": [\n");
        ActionDef[] snapshot = actions;
        for (int i = 0; i < snapshot.Length; ++i) {
            ActionDef def = snapshot[i];
            sb.Append("    { \"name\": \"").Append(def.name).Append("\", \"type\": \"").Append(def.type).Append("\", \"bindings\": [");
            for (int j = 0; j < def.bindings.Count; ++j) {
                Binding b = def.bindings[j];
                sb.Append(SerializeBinding(b));
                if (j + 1 < def.bindings.Count) {
                    sb.Append(", ");
                }
            }
            sb.Append("] }");
            if (i + 1 < snapshot.Length) {
                sb.Append(',');
            }
            sb.Append('\n');
        }
        sb.Append("  ]\n}\n");
        return sb.ToString();
    }

    private static string SerializeBinding(Binding b) {
        switch (b.kind) {
        case BindingKind.Button:
            return $"{{ \"kind\": \"Button\", \"path\": \"{PathOf(b.a)}\" }}";
        case BindingKind.Composite1D:
            return $"{{ \"kind\": \"Axis1D\", \"positive\": \"{PathOf(b.a)}\", \"negative\": \"{PathOf(b.b)}\" }}";
        case BindingKind.Axis1D:
            return $"{{ \"kind\": \"Axis1D\", \"axis\": \"{PathOf(b.a)}\", \"deadZone\": {b.deadZone}, \"sensitivity\": {b.sensitivity}, \"invert\": {(b.invert ? "true" : "false")} }}";
        case BindingKind.Composite2D:
            return $"{{ \"kind\": \"Composite2D\", \"up\": \"{PathOf(b.a)}\", \"down\": \"{PathOf(b.b)}\", \"left\": \"{PathOf(b.c)}\", \"right\": \"{PathOf(b.d)}\" }}";
        case BindingKind.Stick2D:
            return $"{{ \"kind\": \"Stick2D\", \"x\": \"{PathOf(b.a)}\", \"y\": \"{PathOf(b.b)}\", \"deadZone\": {b.deadZone} }}";
        default:
            return "{}";
        }
    }

    private static string PathOf(ResolvedInput input) {
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

    // DLL reload 時に再 parse させる（古い delegate は持たないが、cache は破棄する）。
    internal static void ResetForReload() {
        lock (gate) {
            loaded = false;
            stringIdCache.Clear();
            BlockGameplayInput = false;
        }
    }
}
