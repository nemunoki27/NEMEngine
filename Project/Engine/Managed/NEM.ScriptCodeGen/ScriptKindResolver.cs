using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Diagnostics;
using Microsoft.CodeAnalysis.Text;

using static NEM.ScriptCodeGen.ScriptSchemaRules;
using static NEM.ScriptCodeGen.ScriptSchemaAnalysis;
using static NEM.ScriptCodeGen.ScriptFieldAttributes;
using static NEM.ScriptCodeGen.ScriptIdentity;
using static NEM.ScriptCodeGen.ScriptSchemaText;

namespace NEM.ScriptCodeGen
{
    // スクリプト生成の値種別の解析
    internal static class ScriptKindResolver
    {
        internal static KindInfo ResolveKind(ITypeSymbol type, int depth = 0, Compilation? compilation = null, HashSet<string>? referencePath = null)
        {
            // 配列
            if (type is IArrayTypeSymbol arrayType)
            {
                return arrayType.Rank == 1 ? CollectionKind("Array", ResolveKind(arrayType.ElementType, depth, compilation, referencePath)) :
                    new KindInfo { Kind = "Unsupported" };
            }

            if (type is INamedTypeSymbol named)
            {
                // Nullable<T>
                if (named.IsGenericType && named.ConstructedFrom.SpecialType == SpecialType.System_Nullable_T)
                {
                    return new KindInfo { Kind = "Unsupported" };
                }

                string constructed = named.ConstructedFrom.ToDisplayString();
                if (constructed == "System.Collections.Generic.List<T>")
                {
                    return CollectionKind("List", ResolveKind(named.TypeArguments[0], depth, compilation, referencePath));
                }

                // 参照型は型宣言の基底クラスだけで判定する（MonoBehaviourはComponent派生なので先に判定）
                if (named.TypeKind == TypeKind.Class)
                {
                    if (DerivesFrom(named, AssetFullName))
                    {
                        return new KindInfo { Kind = "AssetRef", AssetType = ResolveAssetType(named) };
                    }
                    if (DerivesFrom(named, MonoBehaviourFullName))
                    {
                        return new KindInfo
                        {
                            Kind = "ScriptRef",
                            ScriptType = named.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
                                .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted)),
                        };
                    }
                    if (!named.IsAbstract && DerivesFrom(named, ComponentFullName))
                    {
                        // 単純名がネイティブのコンポーネント登録名と一致する
                        return new KindInfo { Kind = "ComponentRef", ComponentType = named.Name };
                    }
                }
            }

            // enum
            if (type.TypeKind == TypeKind.Enum && type is INamedTypeSymbol enumType)
            {
                if (enumType.EnumUnderlyingType?.SpecialType is SpecialType.System_Int64 or SpecialType.System_UInt64) {
                    return new KindInfo { Kind = "Unsupported" };
                }
                var info = new KindInfo
                {
                    Kind = "Enum",
                    EnumUnderlying = enumType.EnumUnderlyingType?.SpecialType.ToString() ?? "System_Int32",
                };
                foreach (ISymbol member in enumType.GetMembers())
                {
                    if (member is IFieldSymbol ef && ef.IsConst && ef.HasConstantValue)
                    {
                        info.EnumNames.Add(ef.Name);
                        info.EnumValues.Add(Convert.ToString(ef.ConstantValue, System.Globalization.CultureInfo.InvariantCulture) ?? "0");
                    }
                }
                return info;
            }

            // 既知の scalar / math / reference
            string fullName = type.ToDisplayString();
            string? scalar = ResolveScalarKind(fullName, type.SpecialType);
            if (scalar != null)
            {
                return new KindInfo { Kind = scalar };
            }

            // 既知の型に該当しない[Serializable]クラス/構造体はメンバ展開して編集対象にする
            if (type is INamedTypeSymbol objectType && IsSerializableObjectType(objectType))
            {
                return ResolveObjectKind(objectType, depth, compilation, referencePath);
            }
            return new KindInfo { Kind = "Unsupported" };
        }

        // 多重コンテナと未対応要素を保存対象へ混ぜない
        private static KindInfo CollectionKind(string kind, KindInfo element)
        {
            return element.Kind is "Unsupported" or "Array" or "List" or "Nullable" ? new KindInfo { Kind = "Unsupported" } :
                new KindInfo { Kind = kind, Element = element };
        }

        internal static bool IsSerializableObjectType(INamedTypeSymbol type)
        {
            if (type.IsAbstract || type.IsGenericType || type.SpecialType != SpecialType.None)
            {
                return false;
            }
            if (type.TypeKind != TypeKind.Class && type.TypeKind != TypeKind.Struct)
            {
                return false;
            }
            // エンジンの参照型階層はメンバ展開の対象にしない
            if (DerivesFrom(type, "NEMEngine.Object"))
            {
                return false;
            }
            return type.GetAttributes().Any(a => a.AttributeClass?.ToDisplayString() == SerializableAttributeName);
        }

        internal static KindInfo ResolveObjectKind(INamedTypeSymbol type, int depth, Compilation? compilation = null, HashSet<string>? referencePath = null)
        {
            var info = new KindInfo { Kind = "Object", ObjectType = FullTypeName(type) };

            // 深さ上限を超えた入れ子はメンバ無しで打ち切る(自己参照型の無限展開を防ぐ)
            if (depth >= MaxObjectDepth)
            {
                return info;
            }

            // 継承を含めたメンバ集合(baseから先に、宣言順)を集める
            var declaring = new List<INamedTypeSymbol>();
            for (INamedTypeSymbol? cur = type; cur != null && cur.SpecialType != SpecialType.System_Object; cur = cur.BaseType)
            {
                declaring.Add(cur);
            }
            declaring.Reverse();

            foreach (INamedTypeSymbol owner in declaring)
            {
                foreach (ISymbol member in owner.GetMembers())
                {
                    if (member is not IFieldSymbol field || !IsSerializedField(field))
                    {
                        continue;
                    }
                    info.Members.Add(AnalyzeMemberField(field, depth + 1, compilation, referencePath));
                }
            }
            return info;
        }

        internal static FieldSchema AnalyzeMemberField(IFieldSymbol field, int depth, Compilation? compilation = null, HashSet<string>? referencePath = null)
        {
            var schema = new FieldSchema
            {
                Name = field.Name,
                IsPublic = field.DeclaredAccessibility == Accessibility.Public,
                Location = field.Locations.FirstOrDefault() ?? Location.None,
            };
            ParseFieldAttributes(field, schema);
            bool managedReference = field.GetAttributes().Any(a => a.AttributeClass?.ToDisplayString() == SerializeReferenceAttributeName);
            schema.Kind = managedReference && compilation != null
                ? ResolveSerializeReferenceKind(field.Type, compilation, depth, referencePath)
                : ResolveKind(field.Type, depth, compilation, referencePath);
            return schema;
        }

        internal static KindInfo ResolveSerializeReferenceKind(ITypeSymbol type, Compilation compilation, int depth = 0, HashSet<string>? referencePath = null)
        {
            if (type is IArrayTypeSymbol arrayType)
            {
                return arrayType.Rank == 1 ? CollectionKind("Array", ResolveManagedReferenceKind(arrayType.ElementType, compilation, depth, referencePath)) :
                    new KindInfo { Kind = "Unsupported" };
            }
            if (type is INamedTypeSymbol named && named.IsGenericType &&
                named.ConstructedFrom.ToDisplayString() == "System.Collections.Generic.List<T>")
            {
                return CollectionKind("List", ResolveManagedReferenceKind(named.TypeArguments[0], compilation, depth, referencePath));
            }
            return ResolveManagedReferenceKind(type, compilation, depth, referencePath);
        }

        internal static bool HasSerializableDerivedType(ITypeSymbol type, Compilation compilation)
        {
            ITypeSymbol elementType = type;
            if (type is IArrayTypeSymbol arrayType)
            {
                elementType = arrayType.ElementType;
            }
            else if (type is INamedTypeSymbol listType && listType.IsGenericType &&
                listType.ConstructedFrom.ToDisplayString() == "System.Collections.Generic.List<T>")
            {
                elementType = listType.TypeArguments[0];
            }

            if (elementType is not INamedTypeSymbol namedType ||
                (namedType.TypeKind != TypeKind.Class && namedType.TypeKind != TypeKind.Interface))
            {
                return false;
            }

            KindInfo managedReference = ResolveManagedReferenceKind(namedType, compilation);
            string declaredType = FullTypeName(namedType);
            return managedReference.Candidates.Any(candidate =>
                !string.Equals(candidate.ObjectType, declaredType, StringComparison.Ordinal));
        }

        internal static KindInfo ResolveManagedReferenceKind(ITypeSymbol baseType, Compilation compilation, int depth = 0, HashSet<string>? referencePath = null)
        {
            if (baseType is not INamedTypeSymbol namedBase ||
                namedBase.IsGenericType || namedBase.SpecialType == SpecialType.System_String || DerivesFrom(namedBase, "NEMEngine.Object") ||
                (namedBase.TypeKind != TypeKind.Class && namedBase.TypeKind != TypeKind.Interface))
            {
                return new KindInfo { Kind = "Unsupported" };
            }

            var info = new KindInfo { Kind = "ManagedReference", ObjectType = FullTypeName(namedBase) };
            if (depth >= MaxObjectDepth) { return info; }
            referencePath ??= new HashSet<string>(StringComparer.Ordinal);
            string referenceType = FullTypeName(namedBase);
            if (!referencePath.Add(referenceType)) { return info; }
            try { CollectManagedReferenceCandidates(compilation.Assembly.GlobalNamespace, namedBase, info, compilation, depth, referencePath); }
            finally { referencePath.Remove(referenceType); }
            // 表示と出力を安定させるため型名でソートする
            info.Candidates.Sort((a, b) => string.CompareOrdinal(a.ObjectType, b.ObjectType));
            return info;
        }

        internal static void CollectManagedReferenceCandidates(INamespaceSymbol ns, INamedTypeSymbol baseType, KindInfo info, Compilation compilation, int depth, HashSet<string> referencePath)
        {
            foreach (INamespaceOrTypeSymbol member in ns.GetMembers())
            {
                if (member is INamespaceSymbol childNamespace)
                {
                    CollectManagedReferenceCandidates(childNamespace, baseType, info, compilation, depth, referencePath);
                }
                else if (member is INamedTypeSymbol candidate)
                {
                    CollectManagedReferenceCandidateType(candidate, baseType, info, compilation, depth, referencePath);
                }
            }
        }

        internal static void CollectManagedReferenceCandidateType(INamedTypeSymbol candidate, INamedTypeSymbol baseType, KindInfo info, Compilation compilation, int depth, HashSet<string> referencePath)
        {
            if (IsSerializableObjectType(candidate) && IsAssignableTo(candidate, baseType))
            {
                info.Candidates.Add(ResolveObjectKind(candidate, depth, compilation, referencePath));
            }
            foreach (INamedTypeSymbol nested in candidate.GetTypeMembers())
            {
                CollectManagedReferenceCandidateType(nested, baseType, info, compilation, depth, referencePath);
            }
        }

        internal static bool IsAssignableTo(INamedTypeSymbol candidate, INamedTypeSymbol baseType)
        {
            if (baseType.TypeKind == TypeKind.Interface)
            {
                return candidate.AllInterfaces.Contains(baseType, SymbolEqualityComparer.Default);
            }
            for (INamedTypeSymbol? cur = candidate; cur != null; cur = cur.BaseType)
            {
                if (SymbolEqualityComparer.Default.Equals(cur, baseType))
                {
                    return true;
                }
            }
            return false;
        }

        internal static string FullTypeName(ITypeSymbol type)
        {
            return type.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
                .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted));
        }

        internal static string? ResolveScalarKind(string fullName, SpecialType special)
        {
            switch (special)
            {
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
            switch (fullName)
            {
                case "NEMEngine.Vector2": return "Vector2";
                case "NEMEngine.Vector3": return "Vector3";
                case "NEMEngine.Vector4": return "Vector4";
                case "NEMEngine.Quaternion": return "Quaternion";
                case "NEMEngine.Color3": return "Color3";
                case "NEMEngine.Color4": return "Color4";
                case "NEMEngine.GameObject": return "EntityRef";
            }
            return null;
        }

        internal static bool DerivesFrom(INamedTypeSymbol symbol, string baseFullName)
        {
            for (INamedTypeSymbol? current = symbol.BaseType; current != null; current = current.BaseType)
            {
                if (current.ToDisplayString() == baseFullName)
                {
                    return true;
                }
            }
            return false;
        }

        internal static string ResolveAssetType(ITypeSymbol assetMarker)
        {
            foreach (AttributeData attr in assetMarker.GetAttributes())
            {
                if (attr.AttributeClass?.ToDisplayString() == NativeAssetTypeAttributeName &&
                    attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string nativeType)
                {
                    return nativeType;
                }
            }
            return "Unknown";
        }
    }
}
