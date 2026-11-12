using System.Reflection;
using System.Text.Json.Nodes;

namespace NEMEngine;

// 独自クリップの設定を既存Inspectorと同じ型で列挙する
internal static class TimelineClipFieldSchema {
    internal static JsonObject Describe(FieldInfo field, string id, JsonNode? defaultValue) {
        Type type = field.FieldType;
        var attributes = field.GetCustomAttributesData();
        string kind = Kind(type);
        var result = new JsonObject { ["id"] = id, ["name"] = field.Name,
            ["label"] = Text(attributes, "LabelAttribute") ?? field.Name,
            ["type"] = type.FullName, ["kind"] = kind, ["default"] = defaultValue,
            ["hidden"] = attributes.Any(item => item.AttributeType.Name == "HideInInspectorAttribute"),
            ["readOnly"] = attributes.Any(item => item.AttributeType.Name == "ReadOnlyAttribute"),
            ["tooltip"] = Text(attributes, "TooltipAttribute") ?? string.Empty };
        if (kind == "Asset") result["assetType"] = type.GetCustomAttribute<NativeAssetTypeAttribute>()?.NativeType ?? "Unknown";
        if (kind == "Component") result["componentType"] = type.Name;
        if (kind == "Enum") {
            var options = new JsonArray();
            foreach (string name in Enum.GetNames(type)) {
                object value = Enum.Parse(type, name);
                JsonNode? number = Enum.GetUnderlyingType(type) == typeof(ulong) ?
                    JsonValue.Create(Convert.ToUInt64(value)) : JsonValue.Create(Convert.ToInt64(value));
                options.Add(new JsonObject { ["name"] = name, ["value"] = number });
            }
            result["enumOptions"] = options;
            result["flags"] = type.IsDefined(typeof(FlagsAttribute), false);
        }
        var range = attributes.FirstOrDefault(item => item.AttributeType.Name == "RangeAttribute");
        if (range?.ConstructorArguments.Count == 2) {
            result["min"] = Convert.ToDouble(range.ConstructorArguments[0].Value);
            result["max"] = Convert.ToDouble(range.ConstructorArguments[1].Value);
        }
        return result;
    }

    private static string? Text(IList<CustomAttributeData> attributes, string name) =>
        attributes.FirstOrDefault(item => item.AttributeType.Name == name)?.ConstructorArguments.FirstOrDefault().Value as string;

    private static string Kind(Type type) {
        if (type == typeof(bool)) return "Bool";
        if (type == typeof(int) || type == typeof(uint) || type == typeof(long) || type == typeof(ulong)) return "Int";
        if (type == typeof(float)) return "Float";
        if (type == typeof(double)) return "Double";
        if (type == typeof(string)) return "String";
        if (type == typeof(Vector2)) return "Vector2";
        if (type == typeof(Vector3)) return "Vector3";
        if (type == typeof(Vector4)) return "Vector4";
        if (type == typeof(Color3)) return "Color3";
        if (type == typeof(Color4)) return "Color4";
        if (type == typeof(Quaternion)) return "Quaternion";
        if (type.IsEnum) return "Enum";
        if (typeof(Asset).IsAssignableFrom(type)) return "Asset";
        if (type == typeof(GameObject)) return "GameObject";
        if (typeof(Component).IsAssignableFrom(type) || typeof(MonoBehaviour).IsAssignableFrom(type)) return "Component";
        return "Object";
    }
}
