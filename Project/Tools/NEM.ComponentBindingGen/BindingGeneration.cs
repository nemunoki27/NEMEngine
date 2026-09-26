using System.Text;
using System.Text.Json;
using System.Security.Cryptography;
using System.Buffers.Binary;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// Component連携の生成処理
internal static class BindingGeneration {

    internal static IReadOnlyList<(string path, string text)> Build(string outNativeDir, string outCsDir,
        List<EnumModel> enums, List<ComponentModel> components, List<ComponentModel> bindings, List<AbiFieldModel> abiFields,
        IReadOnlyList<AbiLayoutModel> layouts) {
        enums.Sort((a, b) => string.CompareOrdinal(a.ManagedType, b.ManagedType));
        components.Sort((a, b) => a.ID.CompareTo(b.ID));
        bindings.Sort((a, b) => string.CompareOrdinal(a.RegistryName, b.RegistryName));
        foreach (ComponentModel c in bindings) {
            c.Properties.Sort((a, b) => string.CompareOrdinal(a.ManagedName, b.ManagedName));
        }
        var enumByName = new Dictionary<string, EnumModel>(StringComparer.Ordinal);
        foreach (EnumModel e in enums) enumByName[e.ManagedType] = e;
        var layoutText = ABILayoutEmitter.Emit(layouts, abiFields);
        var outputs = new (string path, string text)[] {
            (Path.Combine(outNativeDir, "ManagedComponentBindings.generated.h"), EmitNativeHeader()),
            (Path.Combine(outNativeDir, "ManagedComponentBindings.generated.cpp"), EmitNativeCpp(bindings, enumByName)),
            (Path.Combine(outNativeDir, "BuiltinComponentRegistry.generated.h"), EmitComponentRegistryHeader()),
            (Path.Combine(outNativeDir, "BuiltinComponentRegistry.generated.cpp"), EmitComponentRegistryCpp(components)),
            (Path.Combine(outNativeDir, "ManagedNativeAPIFields.generated.inl"), EmitNativeAPIFields(abiFields)),
            (Path.Combine(outCsDir, "ComponentBindings.generated.cs"), EmitCSharp(bindings, components, enums)),
            (Path.Combine(outCsDir, "NativeAPITable.generated.cs"), EmitManagedAPITable(abiFields)),
            (Path.Combine(outNativeDir, "ManagedABILayout.generated.inl"), layoutText.native),
            (Path.Combine(outCsDir, "ABILayout.generated.cs"), layoutText.managed),
        };
        // 同じ番号でもpropertyの型や意味が異なる接続を拒否する
        string contract = JsonSerializer.Serialize(new { enums, components, bindings, abiFields, layouts },
            new JsonSerializerOptions { IncludeFields = true });
        byte[] digest = SHA256.HashData(Encoding.UTF8.GetBytes(contract));
        ulong fingerprint = BinaryPrimitives.ReadUInt64LittleEndian(digest);
        outputs[4].text = EmitNativeAPIFields(abiFields, fingerprint);
        outputs[6].text = EmitManagedAPITable(abiFields, fingerprint);
        return outputs;
    }
}
