using System.Collections.Immutable;
using System.Globalization;
using System.Reflection;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;
using NEMEngine.ScriptAnalyzers;

namespace NEM.ScriptAnalyzers.Tests;

// エンジンが使用するコードだけの診断を抑制することを確認する
internal static class ScriptImplicitUsageTests {

	private const string Source = """
using System;
using System.Collections;
using System.Collections.Generic;
using NEMEngine;

public interface IData { }
[Serializable] public sealed class Data : IData { public int value; }
[Serializable] public sealed class SavedData {
    public GameObject target;
    [SerializeField] private string message;
    [NonSerialized] public GameObject transient;
    public GameObject[,] unsupported;
    public GameObject Property { get; set; }
    [field: SerializeField] public GameObject SavedProperty { get; set; }
}
[Serializable] public sealed class ConstructorData {
    [SerializeField] private string saved;
    private string ordinary;
    public ConstructorData() { }
}
public abstract class BaseScript : MonoBehaviour {
    [SerializeField] private Prefab inherited;
    private void Awake() { }
}
public sealed class GameScript : BaseScript {
    [SerializeField] private Prefab particle, otherParticle;
    [SerializeField] private GameObject target;
    [SerializeField] private GameObject[] targets;
    [SerializeField] private List<GameObject> list;
    [SerializeReference] private IData data;
    [SerializeField] private bool active;
    [SerializeField] private float speed = 2.0f;
    [HideInInspector] public GameObject hidden;
    public GameObject publicTarget;
    [SerializeField, NonSerialized] private GameObject ignored;
    private GameObject ordinary;
    [SerializeField] private readonly GameObject readOnly;
    [SerializeField] private static GameObject shared;
    [SerializeField] private Dictionary<string, int> dictionary;
    [SerializeField] private GameObject[][] nested;
    [SerializeField] private List<GameObject[]> nestedList;
    [SerializeReference] private GameObject invalidReference;
    [SerializeField] private longEnum enumValue;
    public bool ReadActive() => active;
    private void Helper() { }
    private void Update(float time) { }
}
public enum longEnum : long { Value }
public sealed class ConstructorScript : MonoBehaviour {
    [SerializeField] private Prefab saved;
    private GameObject ordinary;
    public GameObject Property { get; set; }
    [field: SerializeField] public GameObject SavedProperty { get; set; }
    public ConstructorScript() { }
    public ConstructorScript(int value) { }
}
public sealed class Plain {
    [SerializeField] private GameObject target;
    private void Update() { }
    public string ReadNull(GameObject? value) => value.name;
}
public sealed class WrongSignatures : MonoBehaviour {
    private static void Update() { }
    private int Awake() => 0;
    private IEnumerator LateUpdate() { yield break; }
    private void Start(int value) { }
    private void OnEnable<T>() { }
    private void OnDisable(int value = 0) { }
    private void OnCollisionEnter(ref Collision collision) { }
    private void OnCollisionStay(object collision) { }
    private void OnCollisionExit(in Collision collision) { }
    private void OnAnimationEvent(AnimationEvent value, int other) { }
    private void Helper() { }
}
public sealed class CoroutineStart : MonoBehaviour {
    private IEnumerator Start() { yield break; }
}
""";

	internal static void Run() {

		// 実行時の登録一覧から全コールバックのfixtureを作る
		var callbacks = typeof(NEMEngine.ScriptCallbacks).GetFields(BindingFlags.Instance | BindingFlags.NonPublic);
		string methods = string.Join(Environment.NewLine, callbacks.Select(field => {
			Type[] arguments = field.FieldType.GetGenericArguments().Skip(1).ToArray();
			string parameters = string.Join(", ", arguments.Select((type, index) => $"global::{type.FullName} value{index}"));
			return $"private void {field.Name}({parameters}) {{ }}";
		}));
		SyntaxTree tree = CSharpSyntaxTree.ParseText(Source + "\npublic sealed class ValidCallbacks : MonoBehaviour {\n" + methods + "\n}");
		string[] runtimeAssemblies = ((string)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES")!).Split(Path.PathSeparator);
		var references = runtimeAssemblies.Select(path => MetadataReference.CreateFromFile(path));
		var compilation = CSharpCompilation.Create("ImplicitUsageFixture", new[] { tree }, references,
			new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, nullableContextOptions: NullableContextOptions.Enable));
		var analyzers = ImmutableArray.Create<DiagnosticAnalyzer>(new UsageDiagnostics(), new ScriptImplicitUsageSuppressor());
		var options = new CompilationWithAnalyzersOptions(new AnalyzerOptions(ImmutableArray<AdditionalText>.Empty),
			onAnalyzerException: null, concurrentAnalysis: true, logAnalyzerExecutionTime: false, reportSuppressedDiagnostics: true);
		ImmutableArray<Diagnostic> diagnostics = compilation.WithAnalyzers(analyzers, options).GetAllDiagnosticsAsync().GetAwaiter().GetResult();
		if (diagnostics.Any(diagnostic => diagnostic.Severity == DiagnosticSeverity.Error || diagnostic.Id == "AD0001")) {
			throw new InvalidOperationException(string.Join(Environment.NewLine, diagnostics));
		}

		var savedFields = new HashSet<string>(StringComparer.Ordinal) {
			"Data.value", "SavedData.target", "SavedData.message", "SavedData.SavedProperty", "ConstructorData.saved", "BaseScript.inherited",
			"GameScript.particle", "GameScript.otherParticle", "GameScript.target", "GameScript.targets", "GameScript.list",
			"GameScript.data", "GameScript.active", "GameScript.speed", "GameScript.hidden", "GameScript.publicTarget", "ConstructorScript.saved",
			"ConstructorScript.SavedProperty",
		};
		SemanticModel model = compilation.GetSemanticModel(tree);
		var counts = new Dictionary<string, int>();
		int callbackCount = 0;
		foreach (Diagnostic diagnostic in diagnostics) {
			if (!new[] { "CS8618", "CS0649", "CS0414", "CS0169", "IDE0051", "IDE0052", "IDE0044" }.Contains(diagnostic.Id)) { continue; }
			SyntaxNode node = tree.GetRoot().FindToken(diagnostic.Location.SourceSpan.Start).Parent!;
			string member = GetMember(node, model, diagnostic);
			bool expected = savedFields.Contains(member) || diagnostic.Id == "IDE0051" &&
				(member.StartsWith("ValidCallbacks.", StringComparison.Ordinal) || member is "BaseScript.Awake" or "CoroutineStart.Start");
			if (diagnostic.IsSuppressed != expected) {
				throw new InvalidOperationException($"{diagnostic.Id} {member}: expected suppressed={expected}, actual={diagnostic.IsSuppressed}: {diagnostic}");
			}
			counts[diagnostic.Id] = counts.GetValueOrDefault(diagnostic.Id) + 1;
			if (member.StartsWith("ValidCallbacks.", StringComparison.Ordinal) && diagnostic.IsSuppressed) { ++callbackCount; }
		}
		if (callbackCount != callbacks.Length || counts.Count != 7 ||
			!diagnostics.Any(diagnostic => diagnostic.Id == "CS8602" && !diagnostic.IsSuppressed) ||
			!diagnostics.Any(diagnostic => diagnostic.Id == "CS8618" && diagnostic.IsSuppressed &&
				diagnostic.GetMessage(CultureInfo.InvariantCulture).Contains("'saved'", StringComparison.Ordinal))) {
			throw new InvalidOperationException($"Missing diagnostic coverage: callbacks={callbackCount}/{callbacks.Length}, " +
				string.Join(", ", counts.Select(pair => $"{pair.Key}={pair.Value}")));
		}
		Console.WriteLine($"[PASS] implicit usage: {callbacks.Length} callbacks, seven diagnostic rules and constructor field boundaries.");
	}

	// 診断対象のメンバ名を検証用に取り出す
	private static string GetMember(SyntaxNode node, SemanticModel model, Diagnostic diagnostic) {

		if (node.FirstAncestorOrSelf<VariableDeclaratorSyntax>() is VariableDeclaratorSyntax variable &&
			model.GetDeclaredSymbol(variable) is ISymbol field) {
			return field.ContainingType.Name + "." + field.Name;
		}
		if (node.FirstAncestorOrSelf<MethodDeclarationSyntax>() is MethodDeclarationSyntax method &&
			model.GetDeclaredSymbol(method) is IMethodSymbol symbol) {
			return symbol.ContainingType.Name + "." + symbol.Name;
		}
		if (node.FirstAncestorOrSelf<ConstructorDeclarationSyntax>() is ConstructorDeclarationSyntax constructor &&
			model.GetDeclaredSymbol(constructor) is IMethodSymbol ctor) {
			return ctor.ContainingType.Name + "." + diagnostic.GetMessage(CultureInfo.InvariantCulture).Split('\'')[1];
		}
		if (node.FirstAncestorOrSelf<PropertyDeclarationSyntax>() is PropertyDeclarationSyntax property &&
			model.GetDeclaredSymbol(property) is IPropertySymbol propertySymbol) {
			return propertySymbol.ContainingType.Name + "." + propertySymbol.Name;
		}
		throw new InvalidOperationException("Unknown diagnostic location: " + diagnostic);
	}

	// IDEと同じ診断IDと位置で抑制経路を検証する
#pragma warning disable RS1001 // 検証用に直接起動するため自動検出しない
	private sealed class UsageDiagnostics : DiagnosticAnalyzer {
#pragma warning restore RS1001

		private static readonly ImmutableArray<DiagnosticDescriptor> Rules = new[] { "IDE0051", "IDE0052", "IDE0044" }
			.Select(id => new DiagnosticDescriptor(id, "Unused fixture", "Unused fixture", "Fixture", DiagnosticSeverity.Hidden,
				isEnabledByDefault: true, customTags: new[] { WellKnownDiagnosticTags.Unnecessary })).ToImmutableArray();

		public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics => Rules;

		public override void Initialize(AnalysisContext context) {

			context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);
			context.EnableConcurrentExecution();
			context.RegisterSyntaxNodeAction(analysis => {
				var method = (MethodDeclarationSyntax)analysis.Node;
				analysis.ReportDiagnostic(Diagnostic.Create(Rules[0], method.Identifier.GetLocation()));
			}, SyntaxKind.MethodDeclaration);
			context.RegisterSyntaxNodeAction(analysis => {
				var field = (VariableDeclaratorSyntax)analysis.Node;
				if (analysis.SemanticModel.GetDeclaredSymbol(field) is not IFieldSymbol) { return; }
				foreach (DiagnosticDescriptor rule in Rules) {
					analysis.ReportDiagnostic(Diagnostic.Create(rule, field.Identifier.GetLocation()));
				}
			}, SyntaxKind.VariableDeclarator);
		}
	}
}
