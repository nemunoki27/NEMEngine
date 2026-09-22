using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// 構築引数と検証結果を調停する
internal static class Program {

    private static int Main(string[] args) {

        string? manifestPath = null;
        string? abiPath = null;
        string? outNativeDir = null;
        string? outCsDir = null;
        bool verify = false;
        bool selftest = false;
        for (int i = 0; i < args.Length; ++i) {
            switch (args[i]) {
                case "--manifest": if (i + 1 < args.Length) manifestPath = args[++i]; break;
                case "--metadata": if (i + 1 < args.Length) manifestPath = args[++i]; break;
                case "--abi": if (i + 1 < args.Length) abiPath = args[++i]; break;
                case "--out-native-dir": if (i + 1 < args.Length) outNativeDir = args[++i]; break;
                case "--out-cs-dir": if (i + 1 < args.Length) outCsDir = args[++i]; break;
                case "--verify": verify = true; break;
                case "--selftest": selftest = true; break;
            }
        }

        // selftest は外部入力に依存せず、verify の negative cases を内製の temp 入力で確認する
        if (selftest) {
            return SelfTest();
        }

        if (manifestPath == null || abiPath == null || outNativeDir == null || outCsDir == null) {
            Console.Error.WriteLine("[ComponentBindingGen] usage: --manifest <json> --abi <json> --out-native-dir <dir> --out-cs-dir <dir>");
            return 1;
        }
        if (!File.Exists(manifestPath) || !File.Exists(abiPath)) {
            Console.Error.WriteLine($"[ComponentBindingGen] input not found: manifest={manifestPath} abi={abiPath}");
            return 1;
        }

        var input = new BindingInputReader();
        (List<EnumModel> enums, List<ComponentModel> components) = input.LoadAndValidate(manifestPath);
        List<AbiFieldModel> abiFields = input.LoadAndValidateAbi(abiPath);
        if (input.errorCount > 0) {
            Console.Error.WriteLine($"[ComponentBindingGen] failed with {input.errorCount} validation error(s).");
            return 1;
        }

        List<ComponentModel> bindings = components
            .Where(component => component.Exposure == "GeneratedBinding")
            .ToList();

        // verify モードでは生成せず、Manifestと既存生成物の整合のみを検査する
        if (verify) {
            return Verify(outNativeDir, outCsDir, enums, components, bindings, abiFields);
        }

        // 決定的にするため安定ソート
        var outputs = BindingGeneration.Build(outNativeDir, outCsDir, enums, components, bindings, abiFields);

        Directory.CreateDirectory(outNativeDir);
        Directory.CreateDirectory(outCsDir);

        foreach (var output in outputs) WriteIfChanged(output.path, output.text);

        Console.WriteLine($"[ComponentBindingGen] done. enums={enums.Count} components={components.Count} bindings={bindings.Count} abi={abiFields.Count}");
        return 0;
    }

    private static int Verify(string outNativeDir, string outCsDir, List<EnumModel> enums,
        List<ComponentModel> components, List<ComponentModel> bindings, List<AbiFieldModel> abiFields) {

        int problems = 0;
        void Fail(string message) { ++problems; Console.Error.WriteLine($"[verify] {message}"); }

        var outputs = BindingGeneration.Build(outNativeDir, outCsDir, enums, components, bindings, abiFields);

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

    //========================================================================
    //	selftest（verify の negative cases を内製 temp 入力で確認する）
    //========================================================================
    private static int SelfTest() {

        string temp = Path.Combine(Path.GetTempPath(), "nem_bindinggen_selftest_" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(temp);
        int failures = 0;
        void Expect(string name, bool condition) {
            if (condition) { Console.WriteLine($"[selftest] PASS {name}"); }
            else { ++failures; Console.Error.WriteLine($"[selftest] FAIL {name}"); }
        }

        try {
            string nativeDir = Path.Combine(temp, "native");
            string csDir = Path.Combine(temp, "cs");
            Directory.CreateDirectory(nativeDir);
            Directory.CreateDirectory(csDir);

            const string goodManifest = @"{ ""schemaVersion"": 2, ""enums"": [], ""components"": [
                { ""id"": 0, ""registryName"": ""TestComp"", ""nativeType"": ""TestComponent"", ""nativeHeader"": ""Engine/Test.h"",
                  ""exposure"": ""GeneratedBinding"", ""managedType"": ""TestComp"", ""properties"": [
                    { ""managedName"": ""Target"", ""nativeMember"": ""target"", ""kind"": ""EntityRef"" },
                    { ""managedName"": ""Value"", ""nativeMember"": ""value"", ""kind"": ""Float"" } ] } ] }";
            const string goodAbi = @"{ ""schemaVersion"": 1, ""functions"": [
                { ""name"": ""test"", ""nativeType"": ""TestCallback"", ""managedType"": ""delegate* unmanaged[Cdecl]<int>"" } ] }";

            string manifestPath = Path.Combine(temp, "manifest.json");
            string abiPath = Path.Combine(temp, "abi.json");
            File.WriteAllText(manifestPath, goodManifest);
            File.WriteAllText(abiPath, goodAbi);

            GenerateForSelfTest(manifestPath, abiPath, nativeDir, csDir);
            Expect("EntityRef native dispatch generated",
                File.ReadAllText(Path.Combine(nativeDir, "ManagedComponentBindings.generated.cpp")).Contains("SceneObjectUtility::FindByLocalFileID"));
            Expect("EntityRef managed property generated",
                File.ReadAllText(Path.Combine(csDir, "ComponentBindings.generated.cs")).Contains("public Entity Target"));
            Expect("native registration generated",
                File.ReadAllText(Path.Combine(nativeDir, "BuiltinComponentRegistry.generated.cpp")).Contains("Register<TestComponent>(0"));
            Expect("component mutation notification generated",
                File.ReadAllText(Path.Combine(nativeDir, "ManagedComponentBindings.generated.cpp")).Contains("world.MarkComponentModified<TestComponent>(entity);"));
            Expect("managed ABI generated",
                File.ReadAllText(Path.Combine(csDir, "NativeAPITable.generated.cs")).Contains("delegate* unmanaged[Cdecl]<int> test"));

            Expect("positive verify passes", RunVerify(manifestPath, abiPath, nativeDir, csDir) == 0);

            File.WriteAllText(manifestPath, @"{ ""schemaVersion"": 2, ""enums"": [], ""components"": [
                { ""id"": 1, ""registryName"": ""TestComp"", ""nativeType"": ""TestComponent"", ""nativeHeader"": ""Engine/Test.h"",
                  ""exposure"": ""GeneratedBinding"", ""managedType"": ""TestComp"", ""properties"": [] } ] }");
            Expect("non-contiguous component id fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);

            File.WriteAllText(manifestPath, goodManifest);
            File.WriteAllText(abiPath, @"{ ""schemaVersion"": 1, ""functions"": [
                { ""name"": ""test"", ""nativeType"": ""TestCallback"", ""managedType"": ""delegate* unmanaged[Cdecl]<int>"" },
                { ""name"": ""test"", ""nativeType"": ""TestCallback"", ""managedType"": ""delegate* unmanaged[Cdecl]<int>"" } ] }");
            Expect("duplicate ABI field fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);

            File.WriteAllText(abiPath, goodAbi);
            GenerateForSelfTest(manifestPath, abiPath, nativeDir, csDir);
            File.AppendAllText(Path.Combine(nativeDir, "ManagedComponentBindings.generated.cpp"), "\n// drifted\n");
            Expect("generated drift fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);
        }
        finally {
            try { Directory.Delete(temp, true); } catch { /* best effort cleanup */ }
        }

        Console.WriteLine(failures == 0 ? "[ComponentBindingGen] selftest OK (all negative cases rejected, positive accepted)."
            : $"[ComponentBindingGen] selftest FAILED with {failures} case(s).");
        return failures == 0 ? 0 : 1;
    }

    // selftest 用: schema を読み直して generated 出力を temp へ書く（input.errorCount を独立に扱う）
    private static void GenerateForSelfTest(string manifestPath, string abiPath, string nativeDir, string csDir) {
        var input = new BindingInputReader();
        (List<EnumModel> enums, List<ComponentModel> components) = input.LoadAndValidate(manifestPath);
        List<AbiFieldModel> abiFields = input.LoadAndValidateAbi(abiPath);
        List<ComponentModel> bindings = components.Where(component => component.Exposure == "GeneratedBinding").ToList();
        foreach (var output in BindingGeneration.Build(nativeDir, csDir, enums, components, bindings, abiFields)) {
            File.WriteAllText(output.path, output.text.Replace("\n", Environment.NewLine));
        }
    }

    private static int RunVerify(string manifestPath, string abiPath, string nativeDir, string csDir) {
        var input = new BindingInputReader();
        (List<EnumModel> enums, List<ComponentModel> components) = input.LoadAndValidate(manifestPath);
        List<AbiFieldModel> abiFields = input.LoadAndValidateAbi(abiPath);
        if (input.errorCount > 0) {
            return 1;
        }
        List<ComponentModel> bindings = components.Where(component => component.Exposure == "GeneratedBinding").ToList();
        return Verify(nativeDir, csDir, enums, components, bindings, abiFields);
    }

}
