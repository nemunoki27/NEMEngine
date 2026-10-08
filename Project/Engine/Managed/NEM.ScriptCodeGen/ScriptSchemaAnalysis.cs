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
using static NEM.ScriptCodeGen.ScriptFieldAttributes;
using static NEM.ScriptCodeGen.ScriptKindResolver;
using static NEM.ScriptCodeGen.ScriptIdentity;
using static NEM.ScriptCodeGen.ScriptSchemaText;

namespace NEM.ScriptCodeGen
{
    // スクリプト生成の型とField収集
    internal static class ScriptSchemaAnalysis
    {
        internal static TypeSchema? Analyze(GeneratorSyntaxContext ctx)
        {
            var classDecl = (ClassDeclarationSyntax)ctx.Node;
            if (ctx.SemanticModel.GetDeclaredSymbol(classDecl) is not INamedTypeSymbol symbol)
            {
                return null;
            }
            if (!NEM.ScriptAnalysis.ScriptSymbolRules.IsScript(symbol) ||
                !NEM.ScriptAnalysis.ScriptSymbolRules.IsPrimaryDeclaration(symbol, classDecl))
            {
                return null;
            }

            string fullName = symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
                .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted));
            string scriptTypeID = ResolveScriptTypeID(symbol, fullName, out bool hasExplicitScriptID);

            var schema = new TypeSchema { ScriptTypeID = scriptTypeID, HasExplicitScriptID = hasExplicitScriptID, FullTypeName = fullName };

            // 継承を含めた field 集合（base から先に、宣言順）を集める
            var declaring = new List<INamedTypeSymbol>();
            for (INamedTypeSymbol? cur = symbol; cur != null && cur.ToDisplayString() != MonoBehaviourFullName; cur = cur.BaseType)
            {
                declaring.Add(cur);
            }
            declaring.Reverse();

            foreach (INamedTypeSymbol owner in declaring)
            {
                string ownerName = owner.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
                    .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted));
                foreach (ISymbol member in owner.GetMembers())
                {
                    if (member is not IFieldSymbol field)
                    {
                        continue;
                    }
                    if (!IsSerializedField(field))
                    {
                        continue;
                    }
                    FieldSchema? fieldSchema = AnalyzeField(field, ownerName, scriptTypeID, ctx.SemanticModel.Compilation);
                    if (fieldSchema != null)
                    {
                        schema.Fields.Add(fieldSchema);
                    }
                }
            }
            return schema;
        }

        internal static bool IsSerializedField(IFieldSymbol field) => NEM.ScriptAnalysis.ScriptSymbolRules.IsSerializedField(field);

        internal static FieldSchema? AnalyzeField(IFieldSymbol field, string declaringType, string scriptTypeID, Compilation compilation)
        {
            var schema = new FieldSchema
            {
                Name = field.Name,
                DeclaringType = MetadataTypeName(field.ContainingType),
                DeclaredType = FullTypeName(field.Type),
                IsPublic = field.DeclaredAccessibility == Accessibility.Public,
                Location = field.Locations.FirstOrDefault() ?? Location.None,
            };

            ParseFieldAttributes(field, schema);

            // [SerializeReference]付きは派生型を保存できる参照として扱う
            bool serializeReference = field.GetAttributes()
                .Any(a => a.AttributeClass?.ToDisplayString() == SerializeReferenceAttributeName);
            schema.Kind = serializeReference
                ? ResolveSerializeReferenceKind(field.Type, compilation)
                : ResolveKind(field.Type, compilation: compilation);
            schema.MissingSerializeReference = !serializeReference && HasSerializableDerivedType(field.Type, compilation);

            // origin name は rename を跨いで安定させるため、最も古い FormerlySerializedAs を優先する
            string originName = schema.FormerNames.Count > 0 ? schema.FormerNames[0] : schema.Name;
            if (!string.IsNullOrWhiteSpace(schema.RawID))
            {
                if (TryNormalizeGuid(schema.RawID, out string normalized))
                {
                    schema.FieldID = normalized;
                }
                else
                {
                    schema.RawIDInvalid = true;
                    schema.FieldID = DeterministicGuid("NEMEngine.ScriptField:" + scriptTypeID + "/" + declaringType + "/" + originName);
                }
            }
            else
            {
                schema.FieldID = DeterministicGuid("NEMEngine.ScriptField:" + scriptTypeID + "/" + declaringType + "/" + originName);
            }
            return schema;
        }

        // Reflectionで解決できる型名を保存する
        private static string MetadataTypeName(INamedTypeSymbol type)
        {
            if (type.ContainingType != null) { return MetadataTypeName(type.ContainingType) + "+" + type.MetadataName; }
            return type.ContainingNamespace.IsGlobalNamespace ? type.MetadataName : type.ContainingNamespace.ToDisplayString() + "." + type.MetadataName;
        }

        internal static bool DerivesFromMonoBehaviour(INamedTypeSymbol symbol)
        {
            for (INamedTypeSymbol? current = symbol.BaseType; current != null; current = current.BaseType)
            {
                if (current.ToDisplayString() == MonoBehaviourFullName)
                {
                    return true;
                }
            }
            return false;
        }

        internal static string ResolveScriptTypeID(INamedTypeSymbol symbol, string fullName, out bool hasExplicit)
        {
            foreach (AttributeData attr in symbol.GetAttributes())
            {
                if (attr.AttributeClass?.ToDisplayString() == ScriptTypeIDAttributeName &&
                    attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string idValue &&
                    TryNormalizeGuid(idValue, out string normalized))
                {
                    hasExplicit = true;
                    return normalized;
                }
            }
            hasExplicit = false;
            return DeterministicGuid("NEMEngine.ScriptType:" + fullName);
        }
    }
}
