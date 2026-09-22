using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// Component連携の生成処理
internal static class ManagedBindingEmitter {

    internal static string EmitManagedAPITable(List<AbiFieldModel> fields) {
        var sb = new StringBuilder();
        sb.Append(CsBanner());
        sb.Append("using System.Runtime.InteropServices;\n\n");
        sb.Append("namespace NEMEngine;\n\n");
        sb.Append("[StructLayout(LayoutKind.Sequential)]\n");
        sb.Append("public unsafe struct NativeAPITable {\n\n");
        sb.Append("    public ManagedAbiHeader header;\n");
        foreach (AbiFieldModel field in fields) {
            sb.Append($"    public {field.ManagedType} {field.Name};\n");
        }
        sb.Append("}\n");
        return sb.ToString();
    }

    internal static string EmitCSharp(List<ComponentModel> components,
        List<ComponentModel> allComponents, List<EnumModel> enums) {
        var sb = new StringBuilder();
        sb.Append(CsBanner());
        sb.Append("namespace NEMEngine;\n\n");

        // enums
        foreach (EnumModel e in enums) {
            sb.Append($"public enum {e.ManagedType} {{\n");
            foreach (EnumMember m in e.Members) {
                sb.Append($"    {m.Name} = {m.Value},\n");
            }
            sb.Append("}\n\n");
        }

        // wrappers
        foreach (ComponentModel comp in components) {
            sb.Append($"// {comp.NativeType} の調整可能 property を公開する wrapper（owner Entity の opaque handle のみ保持）\n");
            sb.Append($"public sealed unsafe partial class {comp.ManagedType} : Component, IComponentRef<{comp.ManagedType}> {{\n\n");
            sb.Append($"    internal {comp.ManagedType}(Entity entity) {{ this.entity = entity; }}\n\n");
            sb.Append($"    public static int componentTypeID => {comp.ID};\n");
            sb.Append($"    public static {comp.ManagedType} FromEntity(Entity entity) => new(entity);\n\n");
            sb.Append($"    private const int TypeID = {comp.ID};\n\n");
            for (int p = 0; p < comp.Properties.Count; ++p) {
                EmitCsProperty(sb, comp.Properties[p], p);
            }
            sb.Append("}\n\n");
        }

        List<ComponentModel> factories = allComponents
            .Where(component => !string.IsNullOrEmpty(component.ManagedType) &&
                (component.Exposure == "GeneratedBinding" || component.ManagedFactory))
            .OrderBy(component => component.ID)
            .ToList();

        sb.Append("internal static class GeneratedComponentTypeMap {\n\n");
        sb.Append("    internal static int GetTypeID<T>() where T : Component {\n");
        foreach (ComponentModel component in factories) {
            sb.Append($"        if (typeof(T) == typeof({component.ManagedType})) return {component.ID};\n");
        }
        sb.Append("        return -1;\n");
        sb.Append("    }\n\n");
        sb.Append("    internal static T? Create<T>(Entity entity) where T : Component {\n");
        foreach (ComponentModel component in factories) {
            sb.Append($"        if (typeof(T) == typeof({component.ManagedType})) return (T)(Component)new {component.ManagedType}(entity);\n");
        }
        sb.Append("        return null;\n");
        sb.Append("    }\n");
        sb.Append("}\n");
        return sb.ToString();
    }

    internal static void EmitCsProperty(StringBuilder sb, PropertyModel prop, int propID) {
        switch (prop.Kind) {
            case "Bool": {
                sb.Append($"    {prop.CsVisibility} bool {prop.ManagedName} {{\n");
                sb.Append($"        get {{ int v = 0; NativeAPI.ComponentGet(entity.native, TypeID, {propID}, &v, 4); return v != 0; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ int v = value ? 1 : 0; NativeAPI.ComponentSet(entity.native, TypeID, {propID}, &v, 4); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "Enum": {
                string t = prop.EnumType!;
                sb.Append($"    {prop.CsVisibility} {t} {prop.ManagedName} {{\n");
                sb.Append($"        get {{ int v = 0; NativeAPI.ComponentGet(entity.native, TypeID, {propID}, &v, 4); return ({t})v; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ int v = (int)value; NativeAPI.ComponentSet(entity.native, TypeID, {propID}, &v, 4); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "AssetRef": {
                string t = prop.AssetType!;
                sb.Append($"    {prop.CsVisibility} {t}? {prop.ManagedName} {{\n");
                sb.Append($"        get {{ AssetGUID v = AssetGUID.None; NativeAPI.ComponentGet(entity.native, TypeID, {propID}, &v, 16); return v.isValid ? new {t}(v) : null; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ AssetGUID v = value != null ? value.assetID : AssetGUID.None; NativeAPI.ComponentSet(entity.native, TypeID, {propID}, &v, 16); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "EntityRef": {
                sb.Append($"    {prop.CsVisibility} Entity {prop.ManagedName} {{\n");
                sb.Append($"        get {{ NativeEntity v = NativeEntity.Null; NativeAPI.ComponentGet(entity.native, TypeID, {propID}, &v, sizeof(NativeEntity)); return new Entity(v); }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ NativeEntity v = value.native; NativeAPI.ComponentSet(entity.native, TypeID, {propID}, &v, sizeof(NativeEntity)); }}\n");
                sb.Append("    }\n\n");
                break;
            }
            case "String": {
                sb.Append($"    {prop.CsVisibility} string {prop.ManagedName} {{\n");
                sb.Append($"        get => NativeAPI.ComponentGetString(entity.native, TypeID, {propID});\n");
                if (!prop.ReadOnly) sb.Append($"        set => NativeAPI.ComponentSetString(entity.native, TypeID, {propID}, value);\n");
                sb.Append("    }\n\n");
                break;
            }
            default: {
                (string csType, int size) = PodInfo(prop.Kind);
                sb.Append($"    {prop.CsVisibility} {csType} {prop.ManagedName} {{\n");
                sb.Append($"        get {{ {csType} v = default; NativeAPI.ComponentGet(entity.native, TypeID, {propID}, &v, {size}); return v; }}\n");
                if (!prop.ReadOnly) sb.Append($"        set {{ NativeAPI.ComponentSet(entity.native, TypeID, {propID}, &value, {size}); }}\n");
                sb.Append("    }\n\n");
                break;
            }
        }
    }
}
