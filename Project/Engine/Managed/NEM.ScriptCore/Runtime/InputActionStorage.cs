using System.Text;
using System.Text.Json;
using static NEMEngine.InputActionPath;

namespace NEMEngine;

// Project定義とユーザー設定の入出力を処理する
internal static class InputActionStorage {

    private const string DefaultFileName = "InputActions.json";
    private const string UserFileName = "InputActions.json";

    internal static string? ProjectSettingsDirectory() {
        string root = NativeApplicationAPI.ReadProjectRoot();
        if (string.IsNullOrEmpty(root)) {
            return null;
        }
        return Path.Combine(root, "ProjectSettings");
    }

    internal static string? UserSettingsDirectory() {
        string root = NativeApplicationAPI.ReadUserSettingsRoot();
        if (string.IsNullOrEmpty(root)) {
            return null;
        }
        return Path.Combine(root, "Runtime");
    }

    internal static void TryParseFile(string path, List<InputActionDefinition> outActions) {
        try {
            using JsonDocument doc = JsonDocument.Parse(File.ReadAllText(path));
            JsonElement root = doc.RootElement;
            if (!root.TryGetProperty("actions", out JsonElement actionsEl) || actionsEl.ValueKind != JsonValueKind.Array) {
                return;
            }
            foreach (JsonElement a in actionsEl.EnumerateArray()) {
                InputActionDefinition def = new() {
                    name = a.TryGetProperty("name", out JsonElement n) ? (n.GetString() ?? string.Empty) : string.Empty,
                    type = ParseActionType(a.TryGetProperty("type", out JsonElement t) ? t.GetString() : null),
                };
                if (string.IsNullOrEmpty(def.name)) {
                    continue;
                }
                if (a.TryGetProperty("bindings", out JsonElement bs) && bs.ValueKind == JsonValueKind.Array) {
                    foreach (JsonElement b in bs.EnumerateArray()) {
                        InputActionBinding? binding = ParseBinding(b, def.name);
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
            NativeApplicationAPI.WriteLog(2, $"[InputActions] parse failed: {path}\n{e}");
        }
    }

    internal static InputActionType ParseActionType(string? s) {
        return s switch {
            "Axis1D" => InputActionType.Axis1D,
            "Vector2" => InputActionType.Vector2,
            _ => InputActionType.Button,
        };
    }

    internal static InputActionBinding? ParseBinding(JsonElement b, string actionName) {
        string kind = b.TryGetProperty("kind", out JsonElement k) ? (k.GetString() ?? "Button") : "Button";
        InputActionBinding binding = new();
        binding.deadZone = b.TryGetProperty("deadZone", out JsonElement dz) ? (float)dz.GetDouble() : 0.0f;
        binding.sensitivity = b.TryGetProperty("sensitivity", out JsonElement se) ? (float)se.GetDouble() : 1.0f;
        binding.invert = b.TryGetProperty("invert", out JsonElement iv) && iv.GetBoolean();

        switch (kind) {
        case "Button":
            binding.kind = InputActionBindingKind.Button;
            binding.a = ParsePath(Str(b, "path"), actionName);
            break;
        case "Axis1D":
            if (b.TryGetProperty("axis", out _)) {
                binding.kind = InputActionBindingKind.Axis1D;
                binding.a = ParsePath(Str(b, "axis"), actionName);
            } else {
                binding.kind = InputActionBindingKind.Composite1D;
                binding.a = ParsePath(Str(b, "positive"), actionName);
                binding.b = ParsePath(Str(b, "negative"), actionName);
            }
            break;
        case "Composite2D":
            binding.kind = InputActionBindingKind.Composite2D;
            binding.a = ParsePath(Str(b, "up"), actionName);
            binding.b = ParsePath(Str(b, "down"), actionName);
            binding.c = ParsePath(Str(b, "left"), actionName);
            binding.d = ParsePath(Str(b, "right"), actionName);
            break;
        case "Stick2D":
            binding.kind = InputActionBindingKind.Stick2D;
            binding.a = ParsePath(Str(b, "x"), actionName);
            binding.b = ParsePath(Str(b, "y"), actionName);
            break;
        default:
            NativeApplicationAPI.WriteLog(1, $"[InputActions] unknown binding kind '{kind}' in action '{actionName}'");
            return null;
        }
        return binding;
    }

    internal static string Str(JsonElement e, string key) {
        return e.TryGetProperty(key, out JsonElement v) && v.ValueKind == JsonValueKind.String ? (v.GetString() ?? string.Empty) : string.Empty;
    }

    internal static InputActionDefinition[] LoadFromDisk() {
        var parsed = new List<InputActionDefinition>();
        string? projectDirectory = ProjectSettingsDirectory();
        string? userDirectory = UserSettingsDirectory();
        string userPath = userDirectory == null ? string.Empty : Path.Combine(userDirectory, UserFileName);
        string defaultPath = projectDirectory == null ? string.Empty : Path.Combine(projectDirectory, DefaultFileName);
        string? path = File.Exists(userPath) ? userPath : (File.Exists(defaultPath) ? defaultPath : null);
        if (path != null) {
            TryParseFile(path, parsed);
        }
        return parsed.ToArray();
    }

    internal static bool SaveBindings(InputActionDefinition[] actions) {
        string? dir = UserSettingsDirectory();
        if (dir == null) {
            return false;
        }
        try {
            Directory.CreateDirectory(dir);
            string json = InputActionSerialization.Serialize(actions);
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
            NativeApplicationAPI.WriteLog(2, $"[InputActions] SaveBindings failed\n{e}");
            return false;
        }
    }
}
