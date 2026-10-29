using System.Linq;
using System;
using Microsoft.CodeAnalysis;

namespace NEM.ScriptAnalysis;

// Meta同期と生成器と診断で型と保存Fieldの判定を共有する
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

    // 保存可能な型だけを診断抑制の対象にする
    internal static bool IsSupportedSerializedField(IFieldSymbol field) {
        if (!IsSerializedField(field) || !DerivesFrom(field.ContainingType, "NEMEngine.MonoBehaviour") &&
            !IsSerializableObjectType(field.ContainingType)) {
            return false;
        }
        bool managedReference = field.GetAttributes().Any(attribute =>
            attribute.AttributeClass?.ToDisplayString() == "NEMEngine.SerializeReferenceAttribute");
        return IsSupportedFieldType(field.Type, managedReference);
    }

    // 一重の配列とListを含めて保存型を判定する
    private static bool IsSupportedFieldType(ITypeSymbol type, bool managedReference, bool allowCollection = true) {
        if (type is IArrayTypeSymbol array) {
            return allowCollection && array.Rank == 1 && IsSupportedFieldType(array.ElementType, managedReference, false);
        }
        if (type is INamedTypeSymbol list && list.ConstructedFrom.ToDisplayString() == "System.Collections.Generic.List<T>") {
            return allowCollection && IsSupportedFieldType(list.TypeArguments[0], managedReference, false);
        }
        if (managedReference) {
            return type is INamedTypeSymbol reference && !reference.IsGenericType &&
                reference.SpecialType != SpecialType.System_String && !DerivesFrom(reference, "NEMEngine.Object") &&
                reference.TypeKind is TypeKind.Class or TypeKind.Interface;
        }
        if (type is INamedTypeSymbol named) {
            if (named.TypeKind == TypeKind.Enum) {
                return named.EnumUnderlyingType?.SpecialType is not (SpecialType.System_Int64 or SpecialType.System_UInt64);
            }
            if (named.TypeKind == TypeKind.Class && (DerivesFrom(named, "NEMEngine.Asset") ||
                DerivesFrom(named, "NEMEngine.MonoBehaviour") || !named.IsAbstract && DerivesFrom(named, "NEMEngine.Component"))) {
                return true;
            }
            if (IsSerializableObjectType(named)) { return true; }
        }
        return ResolveScalarKind(type.ToDisplayString(), type.SpecialType) != null;
    }

    // 保存用の値クラスと構造体を判定する
    internal static bool IsSerializableObjectType(INamedTypeSymbol type) {
        return !type.IsAbstract && !type.IsGenericType && type.SpecialType == SpecialType.None &&
            type.TypeKind is TypeKind.Class or TypeKind.Struct && !DerivesFrom(type, "NEMEngine.Object") &&
            type.GetAttributes().Any(attribute => attribute.AttributeClass?.ToDisplayString() == "System.SerializableAttribute");
    }

    // 標準の値型を保存種別へ変換する
    internal static string? ResolveScalarKind(string fullName, SpecialType special) {
        switch (special) {
        case SpecialType.System_Boolean: return "Bool";
        case SpecialType.System_Byte: return "Byte";
        case SpecialType.System_SByte: return "SByte";
        case SpecialType.System_Int16: return "Short";
        case SpecialType.System_UInt16: return "UShort";
        case SpecialType.System_Int32: return "Int";
        case SpecialType.System_UInt32: return "UInt";
        case SpecialType.System_Int64: return "Long";
        case SpecialType.System_UInt64: return "ULong";
        case SpecialType.System_Single: return "Float";
        case SpecialType.System_Double: return "Double";
        case SpecialType.System_String: return "String";
        }
        return fullName switch {
            "NEMEngine.Vector2" => "Vector2",
            "NEMEngine.Vector3" => "Vector3",
            "NEMEngine.Vector4" => "Vector4",
            "NEMEngine.Quaternion" => "Quaternion",
            "NEMEngine.Color3" => "Color3",
            "NEMEngine.Color4" => "Color4",
            "NEMEngine.GameObject" => "EntityRef",
            _ => null,
        };
    }

    // 基底型を宣言名で照合する
    internal static bool DerivesFrom(INamedTypeSymbol type, string baseFullName) {
        for (INamedTypeSymbol? current = type.BaseType; current != null; current = current.BaseType) {
            if (current.ToDisplayString() == baseFullName) { return true; }
        }
        return false;
    }
}
