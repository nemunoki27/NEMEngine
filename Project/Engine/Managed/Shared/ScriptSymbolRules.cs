using System.Linq;
using System;
using Microsoft.CodeAnalysis;

namespace NEM.ScriptAnalysis;

// Meta同期と生成器で型と保存Fieldの判定を共有する
internal static class ScriptSymbolRules {

    internal static bool IsPrimaryDeclaration(INamedTypeSymbol type, SyntaxNode declaration) {
        SyntaxReference primary = type.DeclaringSyntaxReferences.OrderBy(reference => reference.SyntaxTree.FilePath, StringComparer.Ordinal)
            .ThenBy(reference => reference.Span.Start).First();
        return primary.SyntaxTree == declaration.SyntaxTree && primary.Span == declaration.Span;
    }

    internal static bool IsScript(INamedTypeSymbol type) {
        if (type.IsAbstract || type.IsGenericType) { return false; }
        for (INamedTypeSymbol? current = type.BaseType; current != null; current = current.BaseType) {
            if (current.ToDisplayString() == "NEMEngine.MonoBehaviour") { return true; }
        }
        return false;
    }

    internal static bool IsSerializedField(IFieldSymbol field) {
        if (field.IsStatic || field.IsConst || field.IsReadOnly ||
            field.GetAttributes().Any(attribute => attribute.AttributeClass?.ToDisplayString() == "System.NonSerializedAttribute")) {
            return false;
        }
        // 自動生成Fieldは保存属性で明示された場合だけ含める
        return (!field.IsImplicitlyDeclared && field.DeclaredAccessibility == Accessibility.Public) || field.GetAttributes().Any(attribute =>
            attribute.AttributeClass?.ToDisplayString() is "NEMEngine.SerializeFieldAttribute" or "NEMEngine.SerializeReferenceAttribute");
    }
}
