using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using NEM.ScriptAnalysis;

namespace NEM.ScriptMetaSync;

// 実際の型と属性を解決して保存Fieldを収集する
internal static class ScriptSourceCollector {

    internal static Dictionary<string, List<ScriptModel>> Collect(string root) {
#if DEBUG
        string[] symbols = ["TRACE", "DEBUG"];
#elif DEVELOP
        string[] symbols = ["TRACE", "DEVELOP"];
#else
        string[] symbols = ["TRACE"];
#endif
        var options = CSharpParseOptions.Default.WithPreprocessorSymbols(symbols);
        var trees = Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
            .Where(file => !IsExcluded(file)).OrderBy(file => file, StringComparer.Ordinal)
            .Select(file => CSharpSyntaxTree.ParseText(File.ReadAllText(file), options, path: file)).ToList();
        return Collect(trees);
    }

    internal static Dictionary<string, List<ScriptModel>> Collect(IReadOnlyList<SyntaxTree> trees) {
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string? trusted = AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES") as string;
        if (trusted != null) {
            foreach (string path in trusted.Split(Path.PathSeparator)) { paths.Add(path); }
        }
        paths.Add(typeof(NEMEngine.MonoBehaviour).Assembly.Location);
        var references = paths.Select(path => MetadataReference.CreateFromFile(path));
        SyntaxTree globals = CSharpSyntaxTree.ParseText("""
            global using NEMEngine;
            global using System;
            global using System.Collections.Generic;
            global using System.IO;
            global using System.Linq;
            global using System.Net.Http;
            global using System.Threading;
            global using System.Threading.Tasks;
            """, trees.FirstOrDefault()?.Options as CSharpParseOptions);
        var compilation = CSharpCompilation.Create("ScriptMetadataInput", trees.Append(globals), references,
            new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary, allowUnsafe: true));
        return Collect(compilation);
    }

    internal static Dictionary<string, List<ScriptModel>> Collect(CSharpCompilation compilation) {
        Diagnostic[] errors = compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error).ToArray();
        if (errors.Length != 0) {
            throw new InvalidOperationException(string.Join(Environment.NewLine, errors.Select(error => error.ToString())));
        }
        var result = new Dictionary<string, List<ScriptModel>>(StringComparer.OrdinalIgnoreCase);
        var visited = new HashSet<ISymbol>(SymbolEqualityComparer.Default);
        foreach (SyntaxTree tree in compilation.SyntaxTrees.OrderBy(tree => tree.FilePath, StringComparer.Ordinal)) {
            SemanticModel semantic = compilation.GetSemanticModel(tree);
            foreach (ClassDeclarationSyntax declaration in tree.GetRoot().DescendantNodes().OfType<ClassDeclarationSyntax>()) {
                if (semantic.GetDeclaredSymbol(declaration) is not INamedTypeSymbol type ||
                    !visited.Add(type) || !ScriptSymbolRules.IsScript(type)) { continue; }
                ScriptModel model = ReadScript(type);
                if (!result.TryGetValue(tree.FilePath, out List<ScriptModel>? scripts)) {
                    scripts = new();
                    result.Add(tree.FilePath, scripts);
                }
                scripts.Add(model);
            }
        }
        return result;
    }

    private static ScriptModel ReadScript(INamedTypeSymbol type) {
        var result = new ScriptModel {
            FullTypeName = FullName(type),
            ExplicitID = AttributeStrings(type, "NEMEngine.ScriptTypeIDAttribute").FirstOrDefault(),
            FormerlyKnown = AttributeStrings(type, "NEMEngine.FormerlyKnownScriptTypeAttribute"),
        };
        var hierarchy = new Stack<INamedTypeSymbol>();
        for (INamedTypeSymbol? owner = type; owner != null && FullName(owner) != "NEMEngine.MonoBehaviour"; owner = owner.BaseType) {
            hierarchy.Push(owner);
        }
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (INamedTypeSymbol owner in hierarchy) {
            foreach (IFieldSymbol field in owner.GetMembers().OfType<IFieldSymbol>().Where(ScriptSymbolRules.IsSerializedField)) {
                if (!names.Add(field.Name)) { throw new InvalidOperationException($"Duplicate serialized field: {result.FullTypeName}.{field.Name}"); }
                result.Fields.Add(new FieldModel {
                    Name = field.Name,
                    TypeText = field.Type.ToDisplayString(),
                    ExplicitID = AttributeStrings(field, "NEMEngine.SerializedFieldIDAttribute").FirstOrDefault(),
                    FormerlySerializedAs = AttributeStrings(field, "NEMEngine.FormerlySerializedAsAttribute"),
                });
            }
        }
        return result;
    }

    private static string FullName(INamedTypeSymbol type) => type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
        .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted));

    private static List<string> AttributeStrings(ISymbol symbol, string name) {
        return symbol.GetAttributes().Where(attribute => attribute.AttributeClass?.ToDisplayString() == name)
            .Select(attribute => attribute.ConstructorArguments.FirstOrDefault().Value).OfType<string>().ToList();
    }

    internal static bool IsExcluded(string path) {
        string norm = path.Replace('\\', '/');
        string[] excluded = { "/bin/", "/obj/", "/Generated/", "/Staging/", "/Shadow/", "/LastKnownGood/", "/.git/", "/.vs/" };
        foreach (string e in excluded) {
            if (norm.Contains(e, StringComparison.OrdinalIgnoreCase)) { return true; }
        }
        return norm.EndsWith(".g.cs", StringComparison.OrdinalIgnoreCase)
            || norm.EndsWith(".generated.cs", StringComparison.OrdinalIgnoreCase);
    }

}
