using System;
using System.Collections.Immutable;
using System.Globalization;
using System.Linq;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;
using NEM.ScriptAnalysis;

namespace NEMEngine.ScriptAnalyzers;

// エンジンが呼び出すメソッドと保存Fieldの未使用診断を抑制する
[DiagnosticAnalyzer(LanguageNames.CSharp)]
public sealed class ScriptImplicitUsageSuppressor : DiagnosticSuppressor {

	private static readonly ImmutableArray<SuppressionDescriptor> Rules = ImmutableArray.Create(
		new SuppressionDescriptor("NEMSP001", "IDE0051", "エンジンがメソッドと保存Fieldを使用します"),
		new SuppressionDescriptor("NEMSP002", "IDE0052", "エンジンが保存Fieldを読み取ります"),
		new SuppressionDescriptor("NEMSP003", "CS8618", "保存Fieldはコンストラクター完了後に設定します"),
		new SuppressionDescriptor("NEMSP004", "CS0649", "エンジンが保存Fieldへ代入します"),
		new SuppressionDescriptor("NEMSP005", "CS0414", "エンジンが保存Fieldを読み取ります"),
		new SuppressionDescriptor("NEMSP006", "IDE0044", "保存Fieldへの書き込みを維持します"),
		new SuppressionDescriptor("NEMSP007", "CS0169", "エンジンが保存Fieldを使用します"));

	public override ImmutableArray<SuppressionDescriptor> SupportedSuppressions => Rules;

	public override void ReportSuppressions(SuppressionAnalysisContext context) {

		foreach (Diagnostic diagnostic in context.ReportedDiagnostics) {
			if (diagnostic.Location.SourceTree is not SyntaxTree tree) { continue; }
			SuppressionDescriptor? rule = Rules.FirstOrDefault(candidate => candidate.SuppressedDiagnosticId == diagnostic.Id);
			if (rule == null) { continue; }

			SyntaxNode node = tree.GetRoot(context.CancellationToken).FindToken(diagnostic.Location.SourceSpan.Start).Parent!;
			SemanticModel model = context.GetSemanticModel(tree);
			if (diagnostic.Id == "IDE0051" && node.FirstAncestorOrSelf<MethodDeclarationSyntax>() is MethodDeclarationSyntax method &&
				model.GetDeclaredSymbol(method, context.CancellationToken) is IMethodSymbol symbol && IsCallback(symbol)) {
				context.ReportSuppression(Suppression.Create(rule, diagnostic));
				continue;
			}

			IFieldSymbol? field = ResolveField(node, model, diagnostic, context);
			if (field != null && ScriptSymbolRules.IsSupportedSerializedField(field)) {
				context.ReportSuppression(Suppression.Create(rule, diagnostic));
			}
		}
	}

	// 診断位置から対象Fieldを特定する
	private static IFieldSymbol? ResolveField(SyntaxNode node, SemanticModel model, Diagnostic diagnostic,
		SuppressionAnalysisContext context) {

		if (node.FirstAncestorOrSelf<VariableDeclaratorSyntax>() is VariableDeclaratorSyntax variable) {
			return model.GetDeclaredSymbol(variable, context.CancellationToken) as IFieldSymbol;
		}
		if (node.FirstAncestorOrSelf<PropertyDeclarationSyntax>() is PropertyDeclarationSyntax property &&
			model.GetDeclaredSymbol(property, context.CancellationToken) is IPropertySymbol propertySymbol) {
			return propertySymbol.ContainingType.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(field =>
				SymbolEqualityComparer.Default.Equals(field.AssociatedSymbol, propertySymbol));
		}
		if (diagnostic.Id != "CS8618" || node.FirstAncestorOrSelf<ConstructorDeclarationSyntax>() is not ConstructorDeclarationSyntax constructor ||
			model.GetDeclaredSymbol(constructor, context.CancellationToken) is not IMethodSymbol method) {
			return null;
		}

		// コンストラクター上の診断もField単位で照合する
		string message = diagnostic.GetMessage(CultureInfo.InvariantCulture);
		string format = diagnostic.Descriptor.MessageFormat.ToString(CultureInfo.InvariantCulture);
		return method.ContainingType.GetMembers().OfType<IFieldSymbol>().FirstOrDefault(field =>
			string.Equals(message, string.Format(CultureInfo.InvariantCulture, format,
				field.AssociatedSymbol is IPropertySymbol ? "property" : "field", field.AssociatedSymbol?.Name ?? field.Name), StringComparison.Ordinal));
	}

	// 実行時に解決できる署名だけをコールバックとして扱う
	private static bool IsCallback(IMethodSymbol method) {

		if (method.MethodKind != MethodKind.Ordinary || method.IsStatic || method.IsGenericMethod || method.ReturnsByRef ||
			method.ReturnsByRefReadonly || !ScriptSymbolRules.DerivesFrom(method.ContainingType, "NEMEngine.MonoBehaviour")) {
			return false;
		}
		if (!method.ReturnsVoid && (method.Name != "Start" || method.ReturnType.ToDisplayString() != "System.Collections.IEnumerator")) {
			return false;
		}
		string? argument = method.Name switch {
			"Awake" or "Start" or "OnEnable" or "OnDisable" or "OnDestroy" or "FixedUpdate" or "Update" or "LateUpdate" => string.Empty,
			"OnCollisionEnter" or "OnCollisionStay" or "OnCollisionExit" => "NEMEngine.Collision",
			"OnAnimationEvent" => "NEMEngine.AnimationEvent",
			_ => null,
		};
		if (argument == null) { return false; }
		return argument.Length == 0 ? method.Parameters.Length == 0 : method.Parameters.Length == 1 &&
			method.Parameters[0].RefKind == RefKind.None && method.Parameters[0].Type.ToDisplayString() == argument;
	}
}
