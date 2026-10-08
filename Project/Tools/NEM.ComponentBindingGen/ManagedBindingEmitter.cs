using System.Text;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;

namespace NEM.ComponentBindingGen;

// ManagedのComponent参照と接続表を生成する
internal static class ManagedBindingEmitter {

    // Managedの接続関数表を生成する
    internal static string EmitManagedAPITable(List<ABIFieldModel> fields, ulong fingerprint = 0) {
        var sb = new StringBuilder();
        sb.Append(CSBanner());
        sb.Append("using System.Runtime.InteropServices;\n\n");
        sb.Append("namespace NEMEngine;\n\n");
        sb.Append("[StructLayout(LayoutKind.Sequential)]\n");
        sb.Append("public unsafe struct NativeAPITable {\n\n");
        sb.Append($"    internal const ulong BindingFingerprint = 0x{fingerprint:x16}ul;\n");
        sb.Append("    public ManagedABIHeader header;\n");
        foreach (ABIFieldModel field in fields) {
            sb.Append($"    public {field.ManagedType} {field.Name};\n");
        }
        sb.Append("\n    internal readonly bool HasRequiredCallbacks() {\n");
        foreach (ABIFieldModel field in fields) {
            sb.Append($"        if ({field.Name} == null) return false;\n");
        }
        sb.Append("        return true;\n    }\n}\n");
        return sb.ToString();
    }

    // 列挙型とComponent参照を生成する
    internal static string EmitCSharp(List<ComponentModel> components,
        List<ComponentModel> allComponents, List<EnumModel> enums) {
        var sb = new StringBuilder();
        sb.Append(CSBanner());
        sb.Append("namespace NEMEngine;\n\n");

        // 列挙型を生成する
        foreach (EnumModel e in enums) {
            sb.Append($"public enum {e.ManagedType} {{\n");
            foreach (EnumMember m in e.Members) {
                sb.Append($"    {m.Name} = {m.Value},\n");
            }
            sb.Append("}\n\n");
        }

        // Component参照を生成する
        foreach (ComponentModel comp in components) {
            sb.Append($"// {comp.NativeType}の公開プロパティを操作する\n");
            sb.Append($"public sealed unsafe partial class {comp.ManagedType} : Component, IComponentRef<{comp.ManagedType}> {{\n\n");
            sb.Append($"    internal {comp.ManagedType}(GameObject gameObject) {{ this.gameObject = gameObject; }}\n\n");
            sb.Append($"    public static int componentTypeID => {comp.ID};\n");
            sb.Append($"    public static {comp.ManagedType} FromEntity(GameObject gameObject) => new(gameObject);\n\n");
            sb.Append($"    private const int TypeID = {comp.ID};\n\n");
            for (int p = 0; p < comp.Properties.Count; ++p) {
                EmitCSProperty(sb, comp.Properties[p], p);
            }
            sb.Append("}\n\n");
        }

        List<ComponentModel> factories = allComponents
            .Where(component => !string.IsNullOrEmpty(component.ManagedType) &&
                (component.Exposure == "GeneratedBinding" || component.ManagedFactory))
            .OrderBy(component => component.ID)
            .ToList();

        sb.Append("internal static class GeneratedComponentTypeMap {\n\n");
        sb.Append("    internal static int GetTypeID<T>() where T : class => GetTypeID(typeof(T));\n\n");
        sb.Append("    internal static int GetTypeID(Type type) {\n");
        foreach (ComponentModel component in factories) {
            sb.Append($"        if (type == typeof({component.ManagedType})) return {component.ID};\n");
        }
        sb.Append("        return -1;\n");
        sb.Append("    }\n\n");
        sb.Append("    internal static T? Create<T>(GameObject gameObject) where T : class {\n");
        foreach (ComponentModel component in factories) {
            sb.Append($"        if (typeof(T) == typeof({component.ManagedType})) return (T)(object)new {component.ManagedType}(gameObject);\n");
        }
        sb.Append("        return null;\n");
        sb.Append("    }\n");
        sb.Append("\n    internal static void Append<T>(GameObject owner, List<T> results) where T : class {\n");
        foreach (ComponentModel component in factories) {
            sb.Append($"        if (typeof(T).IsAssignableFrom(typeof({component.ManagedType})) && NativeEntityAPI.ReadHasComponent(GameObject.RawNative(owner), {component.ID}))\n");
            sb.Append($"            results.Add((T)(object)new {component.ManagedType}(owner));\n");
        }
        sb.Append("    }\n");
        sb.Append("}\n");
        return sb.ToString();
    }

    // 値型に応じた公開プロパティを生成する
    internal static void EmitCSProperty(StringBuilder sb, PropertyModel prop, int propID) {
        switch (prop.Kind) {
            case "Bool": {
                sb.Append($"    {prop.CSVisibility} bool {prop.ManagedName} {{\n");
                sb.Append($"        get {{ int v = 0; NativeComponentAPI.ComponentGet(native, TypeID, {propID}, &v, 4); return v != 0; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ int v = value ? 1 : 0; NativeComponentAPI.ComponentSet(native, TypeID, {propID}, &v, 4); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "Enum": {
                string t = prop.EnumType!;
                sb.Append($"    {prop.CSVisibility} {t} {prop.ManagedName} {{\n");
                sb.Append($"        get {{ int v = 0; NativeComponentAPI.ComponentGet(native, TypeID, {propID}, &v, 4); return ({t})v; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ int v = (int)value; NativeComponentAPI.ComponentSet(native, TypeID, {propID}, &v, 4); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "AssetRef": {
                string t = prop.AssetType == "RenderPasses" ? "RenderPassesAsset" : prop.AssetType!;
                sb.Append($"    {prop.CSVisibility} {t}? {prop.ManagedName} {{\n");
                sb.Append($"        get {{ AssetGUID v = AssetGUID.None; NativeComponentAPI.ComponentGet(native, TypeID, {propID}, &v, 16); return v.isValid ? new {t}(v) : null; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ AssetGUID v = value != null ? value.assetID : AssetGUID.None; NativeComponentAPI.ComponentSet(native, TypeID, {propID}, &v, 16); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "EntityRef": {
                sb.Append($"    {prop.CSVisibility} GameObject? {prop.ManagedName} {{\n");
                sb.Append($"        get {{ NativeEntity v = NativeEntity.Null; NativeComponentAPI.ComponentGet(native, TypeID, {propID}, &v, sizeof(NativeEntity)); return GameObject.FromNative(v); }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ NativeEntity v = GameObject.RawNative(value); NativeComponentAPI.ComponentSet(native, TypeID, {propID}, &v, sizeof(NativeEntity)); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "String": {
                sb.Append($"    {prop.CSVisibility} string {prop.ManagedName} {{\n");
                sb.Append($"        get => NativeComponentAPI.ComponentGetString(native, TypeID, {propID});\n");
                if (!prop.ReadOnly) sb.Append($"        set => NativeComponentAPI.ComponentSetString(native, TypeID, {propID}, value);\n");
                sb.Append("    }\n\n");
                break;
            }
            default: {
                (string csType, int size) = PODInfo(prop.Kind);
                sb.Append($"    {prop.CSVisibility} {csType} {prop.ManagedName} {{\n");
                sb.Append($"        get {{ {csType} v = default; NativeComponentAPI.ComponentGet(native, TypeID, {propID}, &v, {size}); return v; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ NativeComponentAPI.ComponentSet(native, TypeID, {propID}, &value, {size}); }}\n");
                sb.Append("    }\n\n");
                break;
            }
        }
    }
}
