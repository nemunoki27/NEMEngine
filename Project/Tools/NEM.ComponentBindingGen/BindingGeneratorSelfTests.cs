using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// 一時入力で生成器の正常系と失敗系を検証する
internal static class BindingGeneratorSelfTests {

    // 実際の成果物を変更せず自己検証を実行する
    internal static int Run() {

        string temp = Path.Combine(Directory.GetCurrentDirectory(), ".nem_bindinggen_selftest_" + Guid.NewGuid().ToString("N"));
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
            const string goodABI = @"{ ""schemaVersion"": 1, ""functions"": [
                { ""name"": ""test"", ""nativeType"": ""TestCallback"", ""managedType"": ""delegate* unmanaged[Cdecl]<int>"" } ],
                ""layouts"": [{ ""nativeType"": ""TestValue"", ""managedType"": ""TestValue"", ""size"": 8, ""members"": { ""value"": 0 } }] }";

            string manifestPath = Path.Combine(temp, "manifest.json");
            string abiPath = Path.Combine(temp, "abi.json");
            File.WriteAllText(manifestPath, goodManifest);
            File.WriteAllText(abiPath, goodABI);

            GenerateForSelfTest(manifestPath, abiPath, nativeDir, csDir);
            Expect("EntityRef native dispatch generated",
                File.ReadAllText(Path.Combine(nativeDir, "ManagedComponentBindings.generated.cpp")).Contains("SceneObjectUtility::FindByLocalFileID"));
            Expect("EntityRef managed property generated",
                File.ReadAllText(Path.Combine(csDir, "ComponentBindings.generated.cs")).Contains("public GameObject? Target"));
            Expect("native registration generated",
                File.ReadAllText(Path.Combine(nativeDir, "BuiltinComponentRegistry.generated.cpp")).Contains("Register<TestComponent>(0"));
            Expect("component mutation notification generated",
                File.ReadAllText(Path.Combine(nativeDir, "ManagedComponentBindings.generated.cpp")).Contains("world.MarkComponentModified<TestComponent>(entity);"));
            Expect("managed ABI generated",
                File.ReadAllText(Path.Combine(csDir, "NativeAPITable.generated.cs")).Contains("delegate* unmanaged[Cdecl]<int> test"));

            Expect("positive verify passes", RunVerify(manifestPath, abiPath, nativeDir, csDir) == 0);
            // NativeとManagedへ同じlayoutを出力し、範囲外offsetを拒否する
            Expect("shared layout generated", File.ReadAllText(Path.Combine(csDir, "ABILayout.generated.cs")).Contains("TestValue") &&
                File.ReadAllText(Path.Combine(nativeDir, "ManagedABILayout.generated.inl")).Contains("sizeof(TestValue) == 8"));
            File.WriteAllText(abiPath, goodABI.Replace("\"value\": 0", "\"value\": 8"));
            Expect("out of range member offset fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);
            File.WriteAllText(abiPath, goodABI);
            // 同じサイズのpropertyでも意味が変われば接続識別値を変える
            string originalTable = File.ReadAllText(Path.Combine(csDir, "NativeAPITable.generated.cs"));
            File.WriteAllText(manifestPath, goodManifest.Replace("\"nativeMember\": \"value\"", "\"nativeMember\": \"other\""));
            GenerateForSelfTest(manifestPath, abiPath, nativeDir, csDir);
            Expect("property target changes binding fingerprint",
                File.ReadAllText(Path.Combine(csDir, "NativeAPITable.generated.cs")) != originalTable);
            File.WriteAllText(manifestPath, goodManifest);
            GenerateForSelfTest(manifestPath, abiPath, nativeDir, csDir);
            Expect("binding fingerprint is deterministic",
                File.ReadAllText(Path.Combine(csDir, "NativeAPITable.generated.cs")) == originalTable);
            File.WriteAllText(manifestPath, goodManifest.Replace("\"kind\": \"Float\"", "\"kind\": \"Float\", \"access\": \"WriteOnly\""));
            Expect("invalid access fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);
            File.WriteAllText(manifestPath, "{\"schemaVersion\":2,\"components\":[]}");
            Expect("empty components fail", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);

            // 後の成果物がロックされても先の内容を戻す
            string first = Path.Combine(nativeDir, "rollback-first.txt");
            string second = Path.Combine(nativeDir, "rollback-second.txt");
            File.WriteAllText(first, "before-first");
            File.WriteAllText(second, "before-second");
            bool rejected = false;
            using (var locked = new FileStream(second, FileMode.Open, FileAccess.Read, FileShare.Read)) {
                try { WriteAll(new[] { (first, "after-first"), (second, "after-second") }); }
                catch (AggregateException) { rejected = true; }
            }
            Expect("partial output failure rolls back", rejected && File.ReadAllText(first) == "before-first" &&
                File.ReadAllText(second) == "before-second" && Directory.GetFiles(nativeDir, "*.tmp").Length == 0);

            File.WriteAllText(manifestPath, @"{ ""schemaVersion"": 2, ""enums"": [], ""components"": [
                { ""id"": 1, ""registryName"": ""TestComp"", ""nativeType"": ""TestComponent"", ""nativeHeader"": ""Engine/Test.h"",
                  ""exposure"": ""GeneratedBinding"", ""managedType"": ""TestComp"", ""properties"": [] } ] }");
            Expect("non-contiguous component id fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);

            File.WriteAllText(manifestPath, goodManifest);
            File.WriteAllText(abiPath, @"{ ""schemaVersion"": 1, ""functions"": [
                { ""name"": ""test"", ""nativeType"": ""TestCallback"", ""managedType"": ""delegate* unmanaged[Cdecl]<int>"" },
                { ""name"": ""test"", ""nativeType"": ""TestCallback"", ""managedType"": ""delegate* unmanaged[Cdecl]<int>"" } ] }");
            Expect("duplicate ABI field fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);

            File.WriteAllText(abiPath, goodABI);
            GenerateForSelfTest(manifestPath, abiPath, nativeDir, csDir);
            File.AppendAllText(Path.Combine(nativeDir, "ManagedComponentBindings.generated.cpp"), "\n// drifted\n");
            Expect("generated drift fails", RunVerify(manifestPath, abiPath, nativeDir, csDir) != 0);
        }
        finally {
            try { Directory.Delete(temp, true); } catch { /* 一時入力の削除失敗は検証結果を変えない */ }
        }

        Console.WriteLine(failures == 0 ? "[ComponentBindingGen] selftest OK (all negative cases rejected, positive accepted)."
            : $"[ComponentBindingGen] selftest FAILED with {failures} case(s).");
        return failures == 0 ? 0 : 1;
    }

    // 検証用入力から一時出力を生成する
    private static void GenerateForSelfTest(string manifestPath, string abiPath, string nativeDir, string csDir) {
        var input = new BindingInputReader();
        (List<EnumModel> enums, List<ComponentModel> components) = input.LoadAndValidate(manifestPath);
        List<ABIFieldModel> abiFields = input.LoadAndValidateABI(abiPath);
        List<ComponentModel> bindings = components.Where(component => component.Exposure == "GeneratedBinding").ToList();
        foreach (var output in BindingGeneration.Build(nativeDir, csDir, enums, components, bindings, abiFields, input.layouts)) {
            File.WriteAllText(output.path, output.text.Replace("\n", Environment.NewLine));
        }
    }

    // 入力を検証して保存済みの出力と照合する
    private static int RunVerify(string manifestPath, string abiPath, string nativeDir, string csDir) {
        var input = new BindingInputReader();
        (List<EnumModel> enums, List<ComponentModel> components) = input.LoadAndValidate(manifestPath);
        List<ABIFieldModel> abiFields = input.LoadAndValidateABI(abiPath);
        if (input.errorCount > 0) {
            return 1;
        }
        List<ComponentModel> bindings = components.Where(component => component.Exposure == "GeneratedBinding").ToList();
        return BindingVerification.Run(nativeDir, csDir, enums, components, bindings, abiFields, input.layouts);
    }
}
