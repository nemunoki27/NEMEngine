using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// Component連携の生成処理
internal static class BindingGeneration {

    internal static IReadOnlyList<(string path, string text)> Build(string outNativeDir, string outCsDir,
        List<EnumModel> enums, List<ComponentModel> components, List<ComponentModel> bindings, List<AbiFieldModel> abiFields) {
        enums.Sort((a, b) => string.CompareOrdinal(a.ManagedType, b.ManagedType));
        components.Sort((a, b) => a.ID.CompareTo(b.ID));
        bindings.Sort((a, b) => string.CompareOrdinal(a.RegistryName, b.RegistryName));
        foreach (ComponentModel c in bindings) {
            c.Properties.Sort((a, b) => string.CompareOrdinal(a.ManagedName, b.ManagedName));
        }
        var enumByName = new Dictionary<string, EnumModel>(StringComparer.Ordinal);
        foreach (EnumModel e in enums) enumByName[e.ManagedType] = e;
        return new (string path, string text)[] {
            (Path.Combine(outNativeDir, "ManagedComponentBindings.generated.h"), EmitNativeHeader()),
            (Path.Combine(outNativeDir, "ManagedComponentBindings.generated.cpp"), EmitNativeCpp(bindings, enumByName)),
            (Path.Combine(outNativeDir, "BuiltinComponentRegistry.generated.h"), EmitComponentRegistryHeader()),
            (Path.Combine(outNativeDir, "BuiltinComponentRegistry.generated.cpp"), EmitComponentRegistryCpp(components)),
            (Path.Combine(outNativeDir, "ManagedNativeAPIFields.generated.inl"), EmitNativeAPIFields(abiFields)),
            (Path.Combine(outCsDir, "ComponentBindings.generated.cs"), EmitCSharp(bindings, components, enums)),
            (Path.Combine(outCsDir, "NativeAPITable.generated.cs"), EmitManagedAPITable(abiFields)),
        };
    }
}
