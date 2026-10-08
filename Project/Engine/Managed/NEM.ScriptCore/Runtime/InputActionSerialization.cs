using System.Text;
using static NEMEngine.InputActionPath;

namespace NEMEngine;

// 現在のAction定義を保存形式へ変換する
internal static class InputActionSerialization {

    private const int SchemaVersion = 1;

    internal static string Serialize(InputActionDefinition[] actions) {
        // 現在の action 定義を JSON へ書き出す（user override 用）。device/code を path 文字列へ戻す。
        var sb = new StringBuilder();
        sb.Append("{\n  \"schemaVersion\": ").Append(SchemaVersion).Append(",\n  \"actions\": [\n");
        InputActionDefinition[] snapshot = actions;
        for (int i = 0; i < snapshot.Length; ++i) {
            InputActionDefinition def = snapshot[i];
            sb.Append("    { \"name\": \"").Append(def.name).Append("\", \"type\": \"").Append(def.type).Append("\", \"bindings\": [");
            for (int j = 0; j < def.bindings.Count; ++j) {
                InputActionBinding b = def.bindings[j];
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

    internal static string SerializeBinding(InputActionBinding b) {
        switch (b.kind) {
        case InputActionBindingKind.Button:
            return $"{{ \"kind\": \"Button\", \"path\": \"{PathOf(b.a)}\" }}";
        case InputActionBindingKind.Composite1D:
            return $"{{ \"kind\": \"Axis1D\", \"positive\": \"{PathOf(b.a)}\", \"negative\": \"{PathOf(b.b)}\" }}";
        case InputActionBindingKind.Axis1D:
            return $"{{ \"kind\": \"Axis1D\", \"axis\": \"{PathOf(b.a)}\", \"deadZone\": {b.deadZone}, \"sensitivity\": {b.sensitivity}, \"invert\": {(b.invert ? "true" : "false")} }}";
        case InputActionBindingKind.Composite2D:
            return $"{{ \"kind\": \"Composite2D\", \"up\": \"{PathOf(b.a)}\", \"down\": \"{PathOf(b.b)}\", \"left\": \"{PathOf(b.c)}\", \"right\": \"{PathOf(b.d)}\" }}";
        case InputActionBindingKind.Stick2D:
            return $"{{ \"kind\": \"Stick2D\", \"x\": \"{PathOf(b.a)}\", \"y\": \"{PathOf(b.b)}\", \"deadZone\": {b.deadZone} }}";
        default:
            return "{}";
        }
    }
}
