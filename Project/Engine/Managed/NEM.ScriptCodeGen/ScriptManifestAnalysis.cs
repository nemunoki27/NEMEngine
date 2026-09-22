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

using static NEM.ScriptCodeGen.ScriptManifestRules;
using static NEM.ScriptCodeGen.ScriptIdentity;

namespace NEM.ScriptCodeGen
{
    internal static class ScriptManifestAnalysis
    {
        internal static ScriptTypeModel? Analyze(GeneratorSyntaxContext ctx)
        {
            var classDecl = (ClassDeclarationSyntax)ctx.Node;
            if (ctx.SemanticModel.GetDeclaredSymbol(classDecl) is not INamedTypeSymbol symbol)
            {
                return null;
            }
            // abstract / non-concrete は対象外
            if (symbol.IsAbstract)
            {
                return null;
            }
            if (!DerivesFromScriptBehaviour(symbol))
            {
                return null;
            }

            var model = new ScriptTypeModel
            {
                FullTypeName = symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat.WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted)),
                DisplayName = symbol.Name,
                SourcePath = classDecl.SyntaxTree.FilePath ?? string.Empty,
                Location = classDecl.Identifier.GetLocation(),
            };

            foreach (AttributeData attribute in symbol.GetAttributes())
            {
                string? attrName = attribute.AttributeClass?.ToDisplayString();
                if (attrName == ScriptTypeIDAttributeName)
                {
                    if (attribute.ConstructorArguments.Length == 1 &&
                        attribute.ConstructorArguments[0].Value is string idValue)
                    {
                        model.RawID = idValue;
                        model.HasExplicitID = true;
                    }
                }
            }

            // GUID 正規化（明示が無ければ full type name から決定的 fallback を生成）
            if (model.HasExplicitID)
            {
                if (TryNormalizeGuid(model.RawID, out string normalized))
                {
                    model.NormalizedID = normalized;
                }
                else
                {
                    model.InvalidID = true;
                    model.NormalizedID = DeterministicGuid("NEMEngine.ScriptType:" + model.FullTypeName);
                }
            }
            else
            {
                model.NormalizedID = DeterministicGuid("NEMEngine.ScriptType:" + model.FullTypeName);
            }
            return model;
        }

        internal static bool DerivesFromScriptBehaviour(INamedTypeSymbol symbol)
        {
            for (INamedTypeSymbol? current = symbol.BaseType; current != null; current = current.BaseType)
            {
                if (current.ToDisplayString() == ScriptBehaviourFullName)
                {
                    return true;
                }
            }
            return false;
        }
    }
}
