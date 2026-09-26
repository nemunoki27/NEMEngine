using System;
using System.Collections.Immutable;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;

namespace NEMEngine.ScriptAnalyzers;

//============================================================================
//	ScriptConstructorAnalyzer
//	MonoBehaviour 派生型の constructor 内で行う副作用を warning で検出する analyzer
//============================================================================
[DiagnosticAnalyzer(LanguageNames.CSharp)]
public sealed class ScriptConstructorAnalyzer : DiagnosticAnalyzer {

	private const string Category = "NEMEngine.ScriptLifecycle";
	private const string MonoBehaviourFullName = "NEMEngine.MonoBehaviour";

	// 各 rule。message は「何を / なぜ避けるか」を簡潔に示す。
	private static readonly DiagnosticDescriptor NativeAPIRule = new(
		"NEMSC001", "MonoBehaviour constructor で native API を呼んでいる",
		"MonoBehaviour の constructor で engine API '{0}' を呼んでいます。Awake/Start へ移動してください",
		Category, DiagnosticSeverity.Warning, isEnabledByDefault: true,
		description: "constructor は entity 紐付け・lifecycle より前に呼ばれるため、native/engine API 呼び出しは未初期化参照の原因になります。");

	private static readonly DiagnosticDescriptor EntitySceneRule = new(
		"NEMSC002", "MonoBehaviour constructor で GameObject / Scene を参照している",
		"MonoBehaviour の constructor で GameObject/Scene ('{0}') を参照・操作しています。Awake/Start へ移動してください",
		Category, DiagnosticSeverity.Warning, isEnabledByDefault: true,
		description: "owner entity は constructor 完了後に紐付くため、constructor 内の entity/scene 参照は無効です。");

	private static readonly DiagnosticDescriptor CoroutineRule = new(
		"NEMSC003", "MonoBehaviour constructor で StartCoroutine を呼んでいる",
		"MonoBehaviour の constructor で '{0}' を呼んでいます。コルーチン開始は Start 以降にしてください",
		Category, DiagnosticSeverity.Warning, isEnabledByDefault: true,
		description: "コルーチンは owner と lifecycle に紐付くため、constructor では正しく登録・停止できません。");

	private static readonly DiagnosticDescriptor ThreadingRule = new(
		"NEMSC004", "MonoBehaviour constructor で Task.Run / thread を生成している",
		"MonoBehaviour の constructor で '{0}' を使っています。バックグラウンド処理は lifecycle 後に開始してください",
		Category, DiagnosticSeverity.Warning, isEnabledByDefault: true,
		description: "constructor で生成した thread/Task は instance の lifecycle 管理外になり、停止・破棄が漏れます。");

	private static readonly DiagnosticDescriptor FileIoRule = new(
		"NEMSC005", "MonoBehaviour constructor で file I/O を行っている",
		"MonoBehaviour の constructor で file I/O ('{0}') を行っています。読み込みは Awake/Start へ移動してください",
		Category, DiagnosticSeverity.Warning, isEnabledByDefault: true,
		description: "constructor は instance 生成のたびに走るため、ここでの file I/O は予期しない blocking や多重実行になります。");

	private static readonly DiagnosticDescriptor EventSubscribeRule = new(
		"NEMSC006", "MonoBehaviour constructor で event 購読している",
		"MonoBehaviour の constructor で event '{0}' を購読しています。購読は OnEnable、解除は OnDisable で行ってください",
		Category, DiagnosticSeverity.Warning, isEnabledByDefault: true,
		description: "constructor で購読すると解除契機（OnDisable/OnDestroy）と対にならず、リークやコールバック多重登録になります。");

	public override ImmutableArray<DiagnosticDescriptor> SupportedDiagnostics =>
		ImmutableArray.Create(NativeAPIRule, EntitySceneRule, CoroutineRule, ThreadingRule, FileIoRule, EventSubscribeRule);

	// engine facade（NEMEngine 名前空間の static API 型）。これらの呼び出しは native API 扱い。
	private static readonly string[] EngineFacadeTypes = {
		"NEMEngine.Debug", "NEMEngine.Input", "NEMEngine.Time", "NEMEngine.SceneManager",
		"NEMEngine.Coroutines", "NEMEngine.Timers", "NEMEngine.InputActions",
	};

	public override void Initialize(AnalysisContext context) {

		context.ConfigureGeneratedCodeAnalysis(GeneratedCodeAnalysisFlags.None);
		context.EnableConcurrentExecution();
		context.RegisterSyntaxNodeAction(AnalyzeConstructor, SyntaxKind.ConstructorDeclaration);
		context.RegisterSyntaxNodeAction(AnalyzeInitializer, SyntaxKind.FieldDeclaration, SyntaxKind.PropertyDeclaration);
	}

	private static void AnalyzeConstructor(SyntaxNodeAnalysisContext context) {

		var constructor = (ConstructorDeclarationSyntax)context.Node;
		if (constructor.Body == null && constructor.ExpressionBody == null) {
			return;
		}

		// 宣言型が MonoBehaviour 派生でなければ対象外
		if (context.ContainingSymbol is not IMethodSymbol method) {
			return;
		}
		if (!DerivesFromMonoBehaviour(method.ContainingType)) {
			return;
		}

		SemanticModel model = context.SemanticModel;
		SyntaxNode body = (SyntaxNode?)constructor.Body ?? constructor.ExpressionBody!;

		AnalyzeBody(context, model, body);
	}

	// 初期化子も所有Entityの接続前に実行される
	private static void AnalyzeInitializer(SyntaxNodeAnalysisContext context) {
		INamedTypeSymbol? type = context.ContainingSymbol as INamedTypeSymbol ?? context.ContainingSymbol?.ContainingType;
		if (!DerivesFromMonoBehaviour(type)) { return; }
		if (context.Node is FieldDeclarationSyntax fields) {
			foreach (VariableDeclaratorSyntax field in fields.Declaration.Variables) {
				if (field.Initializer != null) { AnalyzeBody(context, context.SemanticModel, field.Initializer); }
			}
		} else if (context.Node is PropertyDeclarationSyntax property && property.Initializer != null) {
			AnalyzeBody(context, context.SemanticModel, property.Initializer);
		}
	}

	// 後で実行するdelegateやlocal関数の本体は含めない
	private static void AnalyzeBody(SyntaxNodeAnalysisContext context, SemanticModel model, SyntaxNode body) {
		foreach (SyntaxNode node in body.DescendantNodesAndSelf(node =>
			node is not AnonymousFunctionExpressionSyntax && node is not LocalFunctionStatementSyntax)) {

			switch (node) {
			case InvocationExpressionSyntax invocation:
				AnalyzeInvocation(context, model, invocation);
				break;
			case BaseObjectCreationExpressionSyntax creation:
				AnalyzeObjectCreation(context, model, creation);
				break;
			case AssignmentExpressionSyntax assignment:
				AnalyzeAssignment(context, model, assignment);
				break;
			case IdentifierNameSyntax identifier:
				AnalyzeEntitySceneReference(context, model, identifier);
				break;
			}
		}
	}

	private static bool DerivesFromMonoBehaviour(INamedTypeSymbol? type) {

		for (INamedTypeSymbol? current = type; current != null; current = current.BaseType) {
			if (current.ToDisplayString() == MonoBehaviourFullName) {
				return true;
			}
		}
		return false;
	}

	private static void AnalyzeInvocation(SyntaxNodeAnalysisContext context, SemanticModel model, InvocationExpressionSyntax invocation) {

		if (model.GetSymbolInfo(invocation).Symbol is not IMethodSymbol symbol) {
			return;
		}
		string containingType = symbol.ContainingType?.ToDisplayString() ?? string.Empty;
		string containingNamespace = symbol.ContainingNamespace?.ToDisplayString() ?? string.Empty;
		string display = $"{containingType}.{symbol.Name}";

		// StartCoroutine（MonoBehaviour の protected メソッド）
		if (containingType == MonoBehaviourFullName && symbol.Name == "StartCoroutine") {
			context.ReportDiagnostic(Diagnostic.Create(CoroutineRule, invocation.GetLocation(), symbol.Name));
			return;
		}
		// Task.Run / Thread.Start
		if (containingType == "System.Threading.Tasks.Task" && symbol.Name == "Run" ||
			containingType == "System.Threading.Thread" && symbol.Name == "Start") {
			context.ReportDiagnostic(Diagnostic.Create(ThreadingRule, invocation.GetLocation(), display));
			return;
		}
		// file I/O（System.IO 名前空間の呼び出し）
		if ((containingNamespace == "System.IO" || containingNamespace.StartsWith("System.IO.", StringComparison.Ordinal)) &&
			!IsMemoryIO(containingType)) {
			context.ReportDiagnostic(Diagnostic.Create(FileIoRule, invocation.GetLocation(), display));
			return;
		}
		// engine facade（native API 扱い）
		if (EngineFacadeTypes.Contains(containingType)) {
			context.ReportDiagnostic(Diagnostic.Create(NativeAPIRule, invocation.GetLocation(), display));
			return;
		}
	}

	private static void AnalyzeObjectCreation(SyntaxNodeAnalysisContext context, SemanticModel model, BaseObjectCreationExpressionSyntax creation) {

		if (model.GetSymbolInfo(creation).Symbol is not IMethodSymbol ctor) {
			return;
		}
		string type = ctor.ContainingType?.ToDisplayString() ?? string.Empty;
		string ns = ctor.ContainingType?.ContainingNamespace?.ToDisplayString() ?? string.Empty;

		// new Thread(...)
		if (type == "System.Threading.Thread") {
			context.ReportDiagnostic(Diagnostic.Create(ThreadingRule, creation.GetLocation(), type));
			return;
		}
		// new FileStream / StreamReader 等（System.IO の型生成）
		if (ns == "System.IO" && !IsMemoryIO(type)) {
			context.ReportDiagnostic(Diagnostic.Create(FileIoRule, creation.GetLocation(), type));
			return;
		}
	}

	// メモリ上の読み書きとpath加工はファイル操作から除く
	private static bool IsMemoryIO(string type) => type == "System.IO.Path" || type == "System.IO.MemoryStream" ||
		type == "System.IO.StringReader" || type == "System.IO.StringWriter";

	private static void AnalyzeAssignment(SyntaxNodeAnalysisContext context, SemanticModel model, AssignmentExpressionSyntax assignment) {

		// event 購読/解除（+= / -=）。左辺が event symbol のときのみ報告する。
		if (!assignment.IsKind(SyntaxKind.AddAssignmentExpression) && !assignment.IsKind(SyntaxKind.SubtractAssignmentExpression)) {
			return;
		}
		if (model.GetSymbolInfo(assignment.Left).Symbol is IEventSymbol eventSymbol) {
			context.ReportDiagnostic(Diagnostic.Create(EventSubscribeRule, assignment.GetLocation(), eventSymbol.Name));
		}
	}

	private static void AnalyzeEntitySceneReference(SyntaxNodeAnalysisContext context, SemanticModel model, IdentifierNameSyntax identifier) {

		ISymbol? symbol = model.GetSymbolInfo(identifier).Symbol;
		if (symbol == null) {
			return;
		}

		// engineのproperty取得もNative呼出しとして扱う
		if (symbol is IPropertySymbol && EngineFacadeTypes.Contains(symbol.ContainingType?.ToDisplayString())) {
			context.ReportDiagnostic(Diagnostic.Create(NativeAPIRule, identifier.GetLocation(), symbol.Name));
			return;
		}

		// 型が NEMEngine.GameObject / NEMEngine.Scene のローカル参照
		ITypeSymbol? valueType = symbol switch {
			ILocalSymbol local => local.Type,
			IParameterSymbol parameter => parameter.Type,
			IFieldSymbol field => field.Type,
			IPropertySymbol property => property.Type,
			_ => null,
		};
		string valueTypeName = valueType?.ToDisplayString() ?? string.Empty;
		if (valueTypeName == "NEMEngine.GameObject" || valueTypeName == "NEMEngine.SceneHandle") {
			context.ReportDiagnostic(Diagnostic.Create(EntitySceneRule, identifier.GetLocation(), valueTypeName));
		}
	}
}
