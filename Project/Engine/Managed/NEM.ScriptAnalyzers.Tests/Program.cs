using System.Collections.Immutable;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;
using NEM.ScriptCodeGen;
using NEMEngine.ScriptAnalyzers;

namespace NEM.ScriptAnalyzers.Tests;

//============================================================================
//	ScriptConstructorAnalyzer の good/bad fixture 検証ハーネス
//	GUI を起動せず in-memory compile で analyzer 診断を assert する
//============================================================================
internal static class Program {

	// good fixture: constructor は純初期化のみ。NEMSC 診断は 0 件であるべき。
	private const string GoodFixture = @"
using NEMEngine;
namespace SandboxScripts;
public sealed class AnalyzerGoodExample : ScriptBehaviour {
    [SerializeField] private float speed = 1.0f;
    public AnalyzerGoodExample() { speed = 1.0f; }
    public override void Start() { Debug.Log($""good {speed}""); }
}";

	// bad fixture: constructor 内で各副作用を行う（static readonly false ガードで実行はされない）。
	// NEMSC001(native API)/002(Entity)/003(StartCoroutine)/004(Task.Run)/005(file I/O)/006(event) を期待。
	private const string BadFixture = @"
using System;
using System.Collections;
using NEMEngine;
namespace SandboxScripts;
public sealed class AnalyzerBadExample : ScriptBehaviour {
    private static readonly bool FixtureEnabled = false;
    private event Action? OnFixtureTick;
    public AnalyzerBadExample() {
        if (FixtureEnabled) {
            Debug.Log(""side effect"");
            Entity self = entity;
            Debug.Log(self.name);
            StartCoroutine(FixtureRoutine());
            System.Threading.Tasks.Task.Run(() => { });
            using var reader = new System.IO.StreamReader(""never.txt"");
            OnFixtureTick += HandleTick;
        }
    }
    private IEnumerator FixtureRoutine() { yield break; }
    private void HandleTick() { }
}";

	private const string MissingSerializeReferenceFixture = @"
using System;
using NEMEngine;
namespace SandboxScripts;
[Serializable] public class StageUpdater { public float value = 0.0f; }
[Serializable] public sealed class TubeUpdater : StageUpdater { }
public sealed class StageManager : ScriptBehaviour {
    [SerializeField] private StageUpdater stageUpdater = new TubeUpdater();
}";

	private const string SerializeReferenceFixture = @"
using System;
using NEMEngine;
namespace SandboxScripts;
[Serializable] public class StageUpdater { public float value = 0.0f; }
[Serializable] public sealed class TubeUpdater : StageUpdater { }
public sealed class StageManager : ScriptBehaviour {
    [SerializeReference] private StageUpdater stageUpdater = new TubeUpdater();
}";

	private sealed class ProfileFixture : NEMEngine.ScriptBehaviour { }

	// 計測OFF時の区間はNativeを使わず、繰り返してもヒープを確保しない
	private static void TestDisabledScriptProfiler() {
		var owner = new ProfileFixture();
		for (int i = 0; i < 100; ++i) {
			using var sample = NEMEngine.ScriptProfiler.Sample(owner, "warmup");
		}
		long before = GC.GetAllocatedBytesForCurrentThread();
		for (int i = 0; i < 1000; ++i) {
			using var sample = NEMEngine.ScriptProfiler.Sample(owner, "disabled");
		}
		if (GC.GetAllocatedBytesForCurrentThread() != before) {
			throw new InvalidOperationException("Disabled profiler allocated memory");
		}
	}

	private static int Main() {

		int failures = 0;
		try {
			TestDisabledScriptProfiler();
			Console.WriteLine("[PASS] disabled script profiler has no allocations.");
		} catch (Exception ex) {
			++failures;
			Console.Error.WriteLine($"[FAIL] script profiler: {ex}");
		}
		try {
			TestRuntimeInspector();
			Console.WriteLine("[PASS] runtime Inspector filtering and snapshot lifecycle.");
		} catch (Exception ex) {
			++failures;
			Console.Error.WriteLine($"[FAIL] runtime Inspector: {ex}");
		}

		// good: NEMSC 診断 0 件
		ImmutableArray<Diagnostic> goodDiags = Analyze(GoodFixture);
		int goodNem = goodDiags.Count(d => d.Id.StartsWith("NEMSC", StringComparison.Ordinal));
		if (goodNem != 0) {
			++failures;
			Console.Error.WriteLine($"[FAIL] good fixture produced {goodNem} NEMSC diagnostic(s) (expected 0):");
			foreach (Diagnostic d in goodDiags.Where(d => d.Id.StartsWith("NEMSC", StringComparison.Ordinal))) {
				Console.Error.WriteLine($"        {d.Id} {d.GetMessage()}");
			}
		} else {
			Console.WriteLine("[PASS] good fixture: 0 NEMSC diagnostics.");
		}

		// bad: NEMSC001-006 が全て出る
		ImmutableArray<Diagnostic> badDiags = Analyze(BadFixture);
		var badIds = badDiags.Where(d => d.Id.StartsWith("NEMSC", StringComparison.Ordinal))
			.Select(d => d.Id).ToHashSet(StringComparer.Ordinal);
		string[] expected = { "NEMSC001", "NEMSC002", "NEMSC003", "NEMSC004", "NEMSC005", "NEMSC006" };
		foreach (string id in expected) {
			if (!badIds.Contains(id)) {
				++failures;
				Console.Error.WriteLine($"[FAIL] bad fixture missing expected diagnostic {id}.");
			}
		}
		if (failures == 0) {
			Console.WriteLine($"[PASS] bad fixture: all of {string.Join(",", expected)} present (got {badIds.Count} NEMSC ids).");
		}

		// compile error が混ざっていないことも確認（fixture 自体が壊れていないか）
		foreach (string label in new[] { "good", "bad" }) {
			ImmutableArray<Diagnostic> diags = label == "good" ? goodDiags : badDiags;
			int errors = diags.Count(d => d.Severity == DiagnosticSeverity.Error);
			if (errors > 0) {
				++failures;
				Console.Error.WriteLine($"[FAIL] {label} fixture has {errors} compile error(s):");
				foreach (Diagnostic d in diags.Where(d => d.Severity == DiagnosticSeverity.Error)) {
					Console.Error.WriteLine($"        {d.Id} {d.GetMessage()}");
				}
			}
		}

		ImmutableArray<Diagnostic> missingReferenceDiags = GenerateSchema(MissingSerializeReferenceFixture);
		if (!missingReferenceDiags.Any(d => d.Id == "NEMSG015")) {
			++failures;
			Console.Error.WriteLine("[FAIL] polymorphic [SerializeField] did not produce NEMSG015.");
		} else {
			Console.WriteLine("[PASS] polymorphic [SerializeField] produced NEMSG015.");
		}

		ImmutableArray<Diagnostic> serializeReferenceDiags = GenerateSchema(SerializeReferenceFixture);
		if (serializeReferenceDiags.Any(d => d.Id == "NEMSG015")) {
			++failures;
			Console.Error.WriteLine("[FAIL] [SerializeReference] unexpectedly produced NEMSG015.");
		} else {
			Console.WriteLine("[PASS] [SerializeReference] suppressed NEMSG015.");
		}

		Console.WriteLine(failures == 0 ? "ALL TESTS PASSED" : $"{failures} TEST FAILURE(S)");
		return failures == 0 ? 0 : 1;
	}

	// Native境界も含めて取得回数とスナップショットの寿命を確認する
	private static void TestRuntimeInspector() {

		const string source = """
using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Text;
using System.Text.Json.Nodes;
using NEMEngine;

public sealed class RuntimeFixture : ScriptBehaviour {
    public int speed = 3;
    public int[] values = new[] { 1, 2 };
    public Entity missing = Entity.nullEntity;
    public UnsupportedGrid unsupported = new UnsupportedGrid();
    public int hidden = 9;
}

public sealed class UnsupportedGrid {
    public static int reads;
    public Entity?[,] cells = new Entity?[6, 8];
    public int probe { get { ++reads; throw new Exception("Unsupported value was read"); } set { } }
}

public static unsafe class RuntimeTest {
    const BindingFlags PrivateStatic = BindingFlags.NonPublic | BindingFlags.Static;
    const BindingFlags Fields = BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance;
    static object Call(string name, params object[] args) =>
        typeof(HostBridge).GetMethod(name, PrivateStatic)!.Invoke(null, args)!;
    static void Check(bool value, [System.Runtime.CompilerServices.CallerArgumentExpression("value")] string expression = "") {
        if (!value) throw new Exception("Runtime Inspector assertion failed: " + expression);
    }

    public static void Run() {
        var fixture = new RuntimeFixture();
        Type entryType = typeof(HostBridge).GetNestedType("ScriptTypeEntry", BindingFlags.NonPublic)!;
        object entry = Activator.CreateInstance(entryType, true)!;
        var map = (Dictionary<string, FieldInfo>)entryType.GetField("runtimeFieldMap", Fields)!.GetValue(entry)!;
        var fields = (Dictionary<string, FieldInfo>)entryType.GetField("fieldMap", Fields)!.GetValue(entry)!;
        foreach (string name in new[] { "speed", "values", "missing", "unsupported", "hidden" }) {
            var schema = new JsonObject { ["kind"] = name == "unsupported" ? "Unsupported" : "Int",
                ["isHidden"] = name == "hidden", ["isReadOnly"] = name == "values" };
            bool readable = (bool)Call("CanReadRuntimeField", schema);
            Check(readable == (name != "unsupported" && name != "hidden"));
            if (readable) map.Add(name, typeof(RuntimeFixture).GetField(name)!);
            fields.Add(name, typeof(RuntimeFixture).GetField(name)!);
        }
        Check((bool)Call("IsUnsupportedField", new JsonObject { ["kind"] = "unsupported" }));
        var registry = (IDictionary)typeof(HostBridge).GetField("typeToEntry", PrivateStatic)!.GetValue(null)!;
        registry.Add(typeof(RuntimeFixture), entry);
        NativeScriptInstanceHandle handle = (NativeScriptInstanceHandle)Call("AllocateSlot", fixture);
        delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, int*, int> size = &HostBridge.GetRuntimeSerializedStateSize;
        delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, byte*, int, int*, int> copy = &HostBridge.CopyRuntimeSerializedState;
        delegate* unmanaged[Cdecl]<NativeScriptInstanceHandle, byte*, byte*, int> edit = &HostBridge.SetRuntimeSerializedField;
        try {
            int length = 0, written = 0;
            Check(size(handle, &length) == 0 && length > 0);
            fixture.speed = 7;
            Check(copy(handle, null, 0, &written) != 0 && written == length);
            byte[] bytes = new byte[length];
            fixed (byte* buffer = bytes) { Check(copy(handle, buffer, length, &written) == 0); }
            var state = JsonNode.Parse(Encoding.UTF8.GetString(bytes))!;
            Check((int)state["speed"]! == 3 && state["values"]!.AsArray().Count == 2);
            Check(state["missing"] != null && state["unsupported"] == null && state["hidden"] == null);
            Check(UnsupportedGrid.reads == 0);
            fixed (byte* buffer = bytes) { Check(copy(handle, buffer, length, &written) == 0); }
            Check((int)JsonNode.Parse(Encoding.UTF8.GetString(bytes))!["speed"]! == 7);
            Check(size(handle, &length) == 0);
            byte[] field = Encoding.UTF8.GetBytes("speed\0"), value = Encoding.UTF8.GetBytes("5\0");
            fixed (byte* f = field) fixed (byte* v = value) { Check(edit(handle, f, v) == 0); }
            fixed (byte* buffer = bytes) { Check(copy(handle, buffer, bytes.Length, &written) == 0); }
            Check((int)JsonNode.Parse(Encoding.UTF8.GetString(bytes))!["speed"]! == 5);
            Check(size(handle, &length) == 0);
            Call("ReleaseSlot", handle);
            Check(size(handle, &length) != 0);
            handle = (NativeScriptInstanceHandle)Call("AllocateSlot", new RuntimeFixture());
            fixed (byte* buffer = bytes) { Check(copy(handle, buffer, bytes.Length, &written) == 0); }
            Check((int)JsonNode.Parse(Encoding.UTF8.GetString(bytes))!["speed"]! == 3);
            Check(size(handle, &length) == 0);
            Call("ReleaseAllSlots");
            Check(size(handle, &length) != 0);
        } finally {
            Call("ReleaseSlot", handle);
            registry.Remove(typeof(RuntimeFixture));
        }
    }
}
""";
		CSharpCompilation compilation = CreateCompilation(source);
		compilation = compilation.WithOptions(compilation.Options.WithAllowUnsafe(true));
		using var stream = new MemoryStream();
		var result = compilation.Emit(stream);
		if (!result.Success) {
			throw new InvalidOperationException(string.Join(Environment.NewLine, result.Diagnostics));
		}
		var assembly = System.Reflection.Assembly.Load(stream.ToArray());
		assembly.GetType("RuntimeTest")!.GetMethod("Run")!.Invoke(null, null);
	}

	// fixture source を compile し、analyzer 診断 + compile 診断を返す
	private static ImmutableArray<Diagnostic> Analyze(string source) {

		CSharpCompilation compilation = CreateCompilation(source);

		var analyzers = ImmutableArray.Create<DiagnosticAnalyzer>(new ScriptConstructorAnalyzer());
		CompilationWithAnalyzers withAnalyzers = compilation.WithAnalyzers(analyzers);

		// analyzer 診断 + 通常 compile 診断（compile error 検出用）を結合して返す
		ImmutableArray<Diagnostic> analyzerDiags = withAnalyzers.GetAnalyzerDiagnosticsAsync().GetAwaiter().GetResult();
		ImmutableArray<Diagnostic> compileDiags = compilation.GetDiagnostics();
		return analyzerDiags.AddRange(compileDiags);
	}

	private static ImmutableArray<Diagnostic> GenerateSchema(string source) {

		CSharpCompilation compilation = CreateCompilation(source);
		GeneratorDriver driver = CSharpGeneratorDriver.Create(new ScriptSchemaGenerator());
		driver = driver.RunGeneratorsAndUpdateCompilation(compilation, out Compilation output, out _);
		return driver.GetRunResult().Diagnostics.AddRange(output.GetDiagnostics());
	}

	private static CSharpCompilation CreateCompilation(string source) {

		SyntaxTree tree = CSharpSyntaxTree.ParseText(source);
		var references = new List<MetadataReference>();
		string? tpa = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string;
		if (tpa != null) {
			foreach (string path in tpa.Split(Path.PathSeparator)) {
				if (path.Length > 0 && File.Exists(path)) {
					references.Add(MetadataReference.CreateFromFile(path));
				}
			}
		}
		return CSharpCompilation.Create(
			"SchemaFixtureAssembly",
			new[] { tree },
			references,
			new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary,
				nullableContextOptions: NullableContextOptions.Enable));
	}
}
