using System.Text;
using static NEM.ComponentBindingGen.BindingOutputText;

namespace NEM.ComponentBindingGen;

// NativeとManagedのサイズ・配置を同じ定義から検証する
internal static class ABILayoutEmitter {

    internal static (string native, string managed) Emit(IReadOnlyList<AbiLayoutModel> layouts, List<AbiFieldModel> fields) {
        var native = new StringBuilder(NativeBanner());
        var managed = new StringBuilder(CsBanner());
        managed.Append("using System.Runtime.InteropServices;\n\nnamespace NEMEngine;\n\ninternal static class GeneratedABILayout {\n");
        managed.Append("    internal static bool IsValid() {\n");
        var table = new AbiLayoutModel {
            NativeType = "ManagedNativeAPITable", ManagedType = "NativeAPITable", Size = 24 + fields.Count * 8,
        };
        table.Members.Add("header", 0);
        for (int i = 0; i < fields.Count; ++i) { table.Members.Add(fields[i].Name, 24 + i * 8); }
        foreach (AbiLayoutModel layout in layouts.Append(table)) {
            native.Append($"static_assert(sizeof({layout.NativeType}) == {layout.Size});\n");
            managed.Append($"        if (Marshal.SizeOf<{layout.ManagedType}>() != {layout.Size}) return false;\n");
            foreach (var member in layout.Members) {
                native.Append($"static_assert(offsetof({layout.NativeType}, {member.Key}) == {member.Value});\n");
                managed.Append($"        if (Marshal.OffsetOf<{layout.ManagedType}>(\"{member.Key}\").ToInt32() != {member.Value}) return false;\n");
            }
        }
        managed.Append("        return true;\n    }\n}\n");
        return (native.ToString(), managed.ToString());
    }
}
