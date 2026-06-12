using System.Collections.Immutable;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.Diagnostics;
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

	private static int Main() {

		int failures = 0;

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

		Console.WriteLine(failures == 0 ? "ALL TESTS PASSED" : $"{failures} TEST FAILURE(S)");
		return failures == 0 ? 0 : 1;
	}

	// fixture source を compile し、analyzer 診断 + compile 診断を返す
	private static ImmutableArray<Diagnostic> Analyze(string source) {

		SyntaxTree tree = CSharpSyntaxTree.ParseText(source);

		// 参照は実行プロセスの TPA（framework + ScriptCore 等を含む）を全て渡す
		var references = new List<MetadataReference>();
		string? tpa = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string;
		if (tpa != null) {
			foreach (string path in tpa.Split(Path.PathSeparator)) {
				if (path.Length > 0 && File.Exists(path)) {
					references.Add(MetadataReference.CreateFromFile(path));
				}
			}
		}

		var compilation = CSharpCompilation.Create(
			"FixtureAssembly",
			new[] { tree },
			references,
			new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, nullableContextOptions: NullableContextOptions.Enable));

		var analyzers = ImmutableArray.Create<DiagnosticAnalyzer>(new ScriptConstructorAnalyzer());
		CompilationWithAnalyzers withAnalyzers = compilation.WithAnalyzers(analyzers);

		// analyzer 診断 + 通常 compile 診断（compile error 検出用）を結合して返す
		ImmutableArray<Diagnostic> analyzerDiags = withAnalyzers.GetAnalyzerDiagnosticsAsync().GetAwaiter().GetResult();
		ImmutableArray<Diagnostic> compileDiags = compilation.GetDiagnostics();
		return analyzerDiags.AddRange(compileDiags);
	}
}
