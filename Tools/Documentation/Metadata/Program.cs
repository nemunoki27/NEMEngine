using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using NEM.ScriptMetaSync;

namespace NEM.Documentation;

// 公開宣言をゲーム用Assemblyと同じ条件で収集する
internal static class Program {

    private static readonly SymbolDisplayFormat signatureFormat = new(
        typeQualificationStyle: SymbolDisplayTypeQualificationStyle.NameAndContainingTypes,
        genericsOptions: SymbolDisplayGenericsOptions.IncludeTypeParameters | SymbolDisplayGenericsOptions.IncludeTypeConstraints,
        kindOptions: SymbolDisplayKindOptions.IncludeMemberKeyword,
        memberOptions: SymbolDisplayMemberOptions.IncludeType | SymbolDisplayMemberOptions.IncludeParameters |
            SymbolDisplayMemberOptions.IncludeModifiers | SymbolDisplayMemberOptions.IncludeAccessibility |
            SymbolDisplayMemberOptions.IncludeExplicitInterface,
        parameterOptions: SymbolDisplayParameterOptions.IncludeType | SymbolDisplayParameterOptions.IncludeName |
            SymbolDisplayParameterOptions.IncludeDefaultValue | SymbolDisplayParameterOptions.IncludeParamsRefOut,
        miscellaneousOptions: SymbolDisplayMiscellaneousOptions.UseSpecialTypes |
            SymbolDisplayMiscellaneousOptions.IncludeNullableReferenceTypeModifier |
            SymbolDisplayMiscellaneousOptions.EscapeKeywordIdentifiers);

    private static int Main(string[] args) {

        if (args.Length != 2) {
            Console.Error.WriteLine("使い方: DocumentationMetadata <EngineRoot> <OutputJson>");
            return 1;
        }
        string root = Path.GetFullPath(args[0]);
        string project = Path.Combine(root, "Project/Engine/Managed/NEM.ScriptCore/NEM.ScriptCore.csproj");
        var compilation = ScriptCompilationInput.Read(project, "Develop");
        var errors = compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
        if (errors.Length != 0) {
            foreach (var error in errors.Take(30)) Console.Error.WriteLine(error);
            return 2;
        }

        var types = ReadTypes(compilation.Assembly.GlobalNamespace).Where(IsVisible)
            .OrderBy(t => t.ToDisplayString(), StringComparer.Ordinal).Select(type => new {
                id = type.GetDocumentationCommentId(),
                name = type.Name,
                fullName = type.ToDisplayString(),
                kind = type.TypeKind.ToString(),
                signature = type.ToDisplayString(signatureFormat),
                baseType = type.BaseType?.ToDisplayString(),
                source = Source(type, root),
                summary = Summary(type),
                members = type.GetMembers().Where(IsVisibleMember).OrderBy(m => m.Name, StringComparer.Ordinal)
                    .ThenBy(m => m.GetDocumentationCommentId(), StringComparer.Ordinal).Select(member => new {
                        id = member.GetDocumentationCommentId(),
                        name = member.Name,
                        kind = member.Kind.ToString(),
                        signature = member.ToDisplayString(signatureFormat),
                        summary = Summary(member),
                        source = Source(member, root),
                        initializer = Initializer(member),
                        parameters = member is IMethodSymbol method ? method.Parameters.Select(p => new {
                            name = p.Name, type = p.Type.ToDisplayString(), optional = p.IsOptional,
                            refKind = p.RefKind.ToString(),
                            defaultValue = p.HasExplicitDefaultValue ? Convert.ToString(p.ExplicitDefaultValue,
                                System.Globalization.CultureInfo.InvariantCulture) : null
                        }).ToArray() : [],
                        returnType = member switch {
                            IMethodSymbol m => m.ReturnType.ToDisplayString(),
                            IPropertySymbol p => p.Type.ToDisplayString(),
                            IFieldSymbol f => f.Type.ToDisplayString(),
                            IEventSymbol e => e.Type.ToDisplayString(), _ => ""
                        },
                        access = member is IPropertySymbol property ?
                            (IsVisible(property.SetMethod) ? "ReadWrite" : "ReadOnly") :
                            member is IFieldSymbol field && (field.IsConst || field.IsReadOnly) ? "ReadOnly" : "Public"
                    }).ToArray()
            }).ToArray();
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(args[1]))!);
        File.WriteAllText(args[1], JsonSerializer.Serialize(types, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine($"C#公開型: {types.Length} / メンバー: {types.Sum(t => t.members.Length)}");
        return 0;
    }

    // 型の内外を含めて宣言を列挙する
    private static IEnumerable<INamedTypeSymbol> ReadTypes(INamespaceOrTypeSymbol owner) {

        foreach (var member in owner.GetMembers()) {
            if (member is INamespaceSymbol space) {
                foreach (var nested in ReadTypes(space)) yield return nested;
            } else if (member is INamedTypeSymbol type) {
                yield return type;
                foreach (var nested in ReadTypes(type)) yield return nested;
            }
        }
    }

    // 外部のゲームコードから参照できる宣言だけ残す
    private static bool IsVisible(ISymbol? symbol) {

        if (symbol == null || symbol.IsImplicitlyDeclared) return false;
        if (symbol.DeclaredAccessibility is not (Accessibility.Public or Accessibility.Protected or Accessibility.ProtectedOrInternal)) return false;
        return symbol.ContainingType == null || IsVisible(symbol.ContainingType);
    }

    private static bool IsVisibleMember(ISymbol symbol) => IsVisible(symbol) && symbol is not INamedTypeSymbol &&
        (symbol is not IMethodSymbol method || method.MethodKind is MethodKind.Ordinary or MethodKind.Constructor or
            MethodKind.UserDefinedOperator or MethodKind.Conversion or MethodKind.Destructor);

    // 公開物に絶対パスを持ち込まない
    private static string Source(ISymbol symbol, string root) {

        string? path = symbol.Locations.FirstOrDefault(l => l.IsInSource)?.SourceTree?.FilePath;
        return path == null ? "" : Path.GetRelativePath(root, path).Replace('\\', '/');
    }

    // XML説明と直前の短いコメントを取得する
    private static string Summary(ISymbol symbol) {

        string xml = symbol.GetDocumentationCommentXml() ?? "";
        Match summary = Regex.Match(xml, @"<summary>([\s\S]*?)</summary>");
        if (summary.Success) return Regex.Replace(summary.Groups[1].Value, @"<[^>]+>|\s+", " ").Trim();
        var syntax = symbol.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax();
        if (syntax is VariableDeclaratorSyntax) syntax = syntax.Parent?.Parent;
        if (syntax == null) return "";
        return string.Join(" ", syntax.GetLeadingTrivia().Select(t => t.ToString().Trim())
            .Where(t => t.StartsWith("//") && !Regex.IsMatch(t, @"^//[\s=\-]+$"))
            .Select(t => t.TrimStart('/').Trim()).Where(t => t.Length > 0 && !t.Contains("====")));
    }

    private static string? Initializer(ISymbol symbol) {

        var syntax = symbol.DeclaringSyntaxReferences.FirstOrDefault()?.GetSyntax();
        return syntax switch {
            VariableDeclaratorSyntax field => field.Initializer?.Value.ToString(),
            PropertyDeclarationSyntax property => property.Initializer?.Value.ToString(),
            EnumMemberDeclarationSyntax value => value.EqualsValue?.Value.ToString() ??
                (symbol is IFieldSymbol field ? Convert.ToString(field.ConstantValue, System.Globalization.CultureInfo.InvariantCulture) : null), _ => null
        };
    }
}
