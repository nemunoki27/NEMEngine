using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp.Syntax;
using Microsoft.CodeAnalysis.Text;

namespace NEM.ScriptCodeGen
{
    // concrete ScriptBehaviour を列挙し、NEMEngine.GeneratedScriptManifest を生成する。
    // 各型の Stable Script Type GUID / fullTypeName / displayName / sourcePath / 旧名を埋め込む。
    [Generator(LanguageNames.CSharp)]
    public sealed class ScriptManifestGenerator : IIncrementalGenerator
    {
        private const string ScriptBehaviourFullName = "NEMEngine.ScriptBehaviour";
        private const string ScriptTypeIdAttributeName = "NEMEngine.ScriptTypeIdAttribute";
        private const string FormerlyKnownAttributeName = "NEMEngine.FormerlyKnownScriptTypeAttribute";

        private static readonly DiagnosticDescriptor MissingIdRule = new DiagnosticDescriptor(
            "NEMSG001",
            "Script type is missing [ScriptTypeId]",
            "Script type '{0}' has no [ScriptTypeId]; a migration fallback GUID was generated. Add an explicit [ScriptTypeId] before shipping.",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor InvalidIdRule = new DiagnosticDescriptor(
            "NEMSG002",
            "Invalid [ScriptTypeId] value",
            "Script type '{0}' has an invalid [ScriptTypeId] value '{1}'. It must be a GUID. A fallback GUID was generated.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor DuplicateIdRule = new DiagnosticDescriptor(
            "NEMSG003",
            "Duplicate Script Type GUID",
            "Script types '{0}' and '{1}' share the same Script Type GUID '{2}'. GUIDs must be unique.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        private sealed class ScriptTypeModel
        {
            public string FullTypeName = string.Empty;
            public string DisplayName = string.Empty;
            public string SourcePath = string.Empty;
            public string RawId = string.Empty;
            public bool HasExplicitId;
            public string NormalizedId = string.Empty;
            public bool InvalidId;
            public List<string> FormerlyKnown = new List<string>();
            public Location Location = Location.None;
        }

        public void Initialize(IncrementalGeneratorInitializationContext context)
        {
            IncrementalValuesProvider<ScriptTypeModel?> models = context.SyntaxProvider.CreateSyntaxProvider(
                static (node, _) => node is ClassDeclarationSyntax c && c.BaseList != null,
                static (ctx, _) => Analyze(ctx))
                .Where(static m => m != null);

            IncrementalValueProvider<ImmutableArray<ScriptTypeModel?>> collected = models.Collect();

            context.RegisterSourceOutput(collected, static (spc, items) => Emit(spc, items));
        }

        private static ScriptTypeModel? Analyze(GeneratorSyntaxContext ctx)
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
                if (attrName == ScriptTypeIdAttributeName)
                {
                    if (attribute.ConstructorArguments.Length == 1 &&
                        attribute.ConstructorArguments[0].Value is string idValue)
                    {
                        model.RawId = idValue;
                        model.HasExplicitId = true;
                    }
                }
                else if (attrName == FormerlyKnownAttributeName)
                {
                    if (attribute.ConstructorArguments.Length == 1 &&
                        attribute.ConstructorArguments[0].Value is string formerName &&
                        !string.IsNullOrWhiteSpace(formerName))
                    {
                        model.FormerlyKnown.Add(formerName);
                    }
                }
            }

            // GUID 正規化（明示が無ければ full type name から決定的 fallback を生成）
            if (model.HasExplicitId)
            {
                if (TryNormalizeGuid(model.RawId, out string normalized))
                {
                    model.NormalizedId = normalized;
                }
                else
                {
                    model.InvalidId = true;
                    model.NormalizedId = DeterministicGuid(model.FullTypeName);
                }
            }
            else
            {
                model.NormalizedId = DeterministicGuid(model.FullTypeName);
            }
            return model;
        }

        private static bool DerivesFromScriptBehaviour(INamedTypeSymbol symbol)
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

        private static void Emit(SourceProductionContext spc, ImmutableArray<ScriptTypeModel?> items)
        {
            var models = items.Where(m => m != null).Select(m => m!).ToList();

            // 診断: 明示 ID 欠落 / 不正 / 重複
            foreach (ScriptTypeModel model in models)
            {
                if (!model.HasExplicitId)
                {
                    spc.ReportDiagnostic(Diagnostic.Create(MissingIdRule, model.Location, model.FullTypeName));
                }
                else if (model.InvalidId)
                {
                    spc.ReportDiagnostic(Diagnostic.Create(InvalidIdRule, model.Location, model.FullTypeName, model.RawId));
                }
            }

            var seen = new Dictionary<string, ScriptTypeModel>(StringComparer.Ordinal);
            foreach (ScriptTypeModel model in models)
            {
                if (seen.TryGetValue(model.NormalizedId, out ScriptTypeModel? other))
                {
                    spc.ReportDiagnostic(Diagnostic.Create(DuplicateIdRule, model.Location,
                        model.FullTypeName, other.FullTypeName, model.NormalizedId));
                }
                else
                {
                    seen[model.NormalizedId] = model;
                }
            }

            var builder = new StringBuilder();
            builder.AppendLine("// <auto-generated/>");
            builder.AppendLine("#nullable enable");
            builder.AppendLine("namespace NEMEngine");
            builder.AppendLine("{");
            builder.AppendLine("    internal static class GeneratedScriptManifest");
            builder.AppendLine("    {");
            builder.AppendLine("        public static global::NEMEngine.ScriptTypeDescriptor[] GetDescriptors()");
            builder.AppendLine("        {");
            builder.AppendLine("            return new global::NEMEngine.ScriptTypeDescriptor[]");
            builder.AppendLine("            {");
            foreach (ScriptTypeModel model in models.OrderBy(m => m.FullTypeName, StringComparer.Ordinal))
            {
                string former = model.FormerlyKnown.Count == 0
                    ? "global::System.Array.Empty<string>()"
                    : "new string[] { " + string.Join(", ", model.FormerlyKnown.Select(EscapeString)) + " }";
                builder.Append("                new global::NEMEngine.ScriptTypeDescriptor(");
                builder.Append(EscapeString(model.NormalizedId)).Append(", ");
                builder.Append(EscapeString(model.FullTypeName)).Append(", ");
                builder.Append(EscapeString(model.DisplayName)).Append(", ");
                builder.Append(EscapeString(model.SourcePath)).Append(", ");
                builder.Append(model.HasExplicitId && !model.InvalidId ? "true" : "false").Append(", ");
                builder.Append(former);
                builder.AppendLine("),");
            }
            builder.AppendLine("            };");
            builder.AppendLine("        }");
            builder.AppendLine("    }");
            builder.AppendLine("}");

            spc.AddSource("GeneratedScriptManifest.g.cs", SourceText.From(builder.ToString(), Encoding.UTF8));
        }

        private static string EscapeString(string value)
        {
            return "\"" + value.Replace("\\", "\\\\").Replace("\"", "\\\"") + "\"";
        }

        // GUID を小文字ハイフン形式へ正規化する
        private static bool TryNormalizeGuid(string raw, out string normalized)
        {
            if (Guid.TryParse(raw, out Guid guid))
            {
                normalized = guid.ToString("D");
                return true;
            }
            normalized = string.Empty;
            return false;
        }

        // full type name から決定的な fallback GUID を作る（移行用。明示 ID 推奨）
        private static string DeterministicGuid(string fullTypeName)
        {
            using var md5 = MD5.Create();
            byte[] hash = md5.ComputeHash(Encoding.UTF8.GetBytes("NEMEngine.ScriptType:" + fullTypeName));
            return new Guid(hash).ToString("D");
        }
    }
}
