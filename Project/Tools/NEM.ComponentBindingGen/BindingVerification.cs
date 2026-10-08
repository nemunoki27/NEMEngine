using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// 入力と保存済みの成果物を照合する
internal static class BindingVerification {

    // 生成と同じ出力集合の差分を検査する
    internal static int Run(string outNativeDir, string outCSDir, List<EnumModel> enums,
        List<ComponentModel> components, List<ComponentModel> bindings, List<ABIFieldModel> abiFields, List<ABILayoutModel> layouts) {

        int problems = 0;
        void Fail(string message) { ++problems; Console.Error.WriteLine($"[verify] {message}"); }

        var outputs = BindingGeneration.Build(outNativeDir, outCSDir, enums, components, bindings, abiFields, layouts);

        foreach (var output in outputs) CheckDrift(output.path, output.text, Fail);

        foreach (ComponentModel c in bindings) {
            for (int p = 0; p < c.Properties.Count; ++p) {
                if (!IsKnownKind(c.Properties[p].Kind)) {
                    Fail($"{c.ManagedType}.{c.Properties[p].ManagedName}: unsupported kind '{c.Properties[p].Kind}'.");
                }
            }
        }

        if (problems > 0) {
            Console.Error.WriteLine($"[ComponentBindingGen] verify FAILED with {problems} problem(s).");
            return 1;
        }
        Console.WriteLine($"[ComponentBindingGen] verify OK. components={components.Count} bindings={bindings.Count} abi={abiFields.Count}");
        return 0;
    }
}
