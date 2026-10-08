using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// 構築引数と検証結果を調停する
internal static class Program {

    // 起動引数に応じて生成と検証を実行する
    private static int Main(string[] args) {

        string? manifestPath = null;
        string? abiPath = null;
        string? outNativeDir = null;
        string? outCSDir = null;
        bool verify = false;
        bool selftest = false;
        for (int i = 0; i < args.Length; ++i) {
            switch (args[i]) {
                case "--manifest": if (i + 1 < args.Length) manifestPath = args[++i]; break;
                case "--metadata": if (i + 1 < args.Length) manifestPath = args[++i]; break;
                case "--abi": if (i + 1 < args.Length) abiPath = args[++i]; break;
                case "--out-native-dir": if (i + 1 < args.Length) outNativeDir = args[++i]; break;
                case "--out-cs-dir": if (i + 1 < args.Length) outCSDir = args[++i]; break;
                case "--verify": verify = true; break;
                case "--selftest": selftest = true; break;
            }
        }

        // 実際のAssetを変更せず正常系と失敗系を検証する
        if (selftest) {
            return BindingGeneratorSelfTests.Run();
        }

        if (manifestPath == null || abiPath == null || outNativeDir == null || outCSDir == null) {
            Console.Error.WriteLine("[ComponentBindingGen] usage: --manifest <json> --abi <json> --out-native-dir <dir> --out-cs-dir <dir>");
            return 1;
        }
        if (!File.Exists(manifestPath) || !File.Exists(abiPath)) {
            Console.Error.WriteLine($"[ComponentBindingGen] input not found: manifest={manifestPath} abi={abiPath}");
            return 1;
        }

        var input = new BindingInputReader();
        (List<EnumModel> enums, List<ComponentModel> components) = input.LoadAndValidate(manifestPath);
        List<ABIFieldModel> abiFields = input.LoadAndValidateABI(abiPath);
        if (input.errorCount > 0) {
            Console.Error.WriteLine($"[ComponentBindingGen] failed with {input.errorCount} validation error(s).");
            return 1;
        }

        List<ComponentModel> bindings = components
            .Where(component => component.Exposure == "GeneratedBinding")
            .ToList();

        // 保存せず入力と生成済みの成果物を照合する
        if (verify) {
            return BindingVerification.Run(outNativeDir, outCSDir, enums, components, bindings, abiFields, input.layouts);
        }

        // 同じ入力から同じ順序の成果物を作る
        var outputs = BindingGeneration.Build(outNativeDir, outCSDir, enums, components, bindings, abiFields, input.layouts);

        Directory.CreateDirectory(outNativeDir);
        Directory.CreateDirectory(outCSDir);

        WriteAll(outputs);

        Console.WriteLine($"[ComponentBindingGen] done. enums={enums.Count} components={components.Count} bindings={bindings.Count} abi={abiFields.Count}");
        return 0;
    }
}
