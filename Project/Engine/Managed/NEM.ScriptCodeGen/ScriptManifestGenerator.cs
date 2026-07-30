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

namespace NEM.ScriptCodeGen
{
    // concrete ScriptBehaviour を列挙し、NEMEngine.GeneratedScriptManifest を生成する。
    // 各型の Stable Script Type GUID / fullTypeName / displayName / sourcePath を埋め込む。
    [Generator(LanguageNames.CSharp)]
    public sealed class ScriptManifestGenerator : IIncrementalGenerator
    {
        private const string ScriptBehaviourFullName = "NEMEngine.ScriptBehaviour";
        private const string ScriptTypeIdAttributeName = "NEMEngine.ScriptTypeIdAttribute";

        private static readonly DiagnosticDescriptor MissingIdRule = new DiagnosticDescriptor(
            "NEMSG001",
            "Script type has no stable id",
            "Script type '{0}' has no stable Script Type GUID (no [ScriptTypeId] and no .cs.meta entry); a temporary fallback GUID was generated. Run Editor metadata sync.",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor MissingIdValidateRule = new DiagnosticDescriptor(
            "NEMSG004",
            "Script type metadata is missing",
            "Script type '{0}' has no stable Script Type GUID and metadata mode is ValidateOnly. Run Editor metadata sync to generate .cs.meta.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

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
            // 明示属性 or sidecar metadata で安定 ID が得られたか（false は決定的 fallback）
            public bool HasStableId;
            public Location Location = Location.None;
        }

        public void Initialize(IncrementalGeneratorInitializationContext context)
        {
            IncrementalValuesProvider<ScriptTypeModel?> models = context.SyntaxProvider.CreateSyntaxProvider(
                static (node, _) => node is ClassDeclarationSyntax c && c.BaseList != null,
                static (ctx, _) => Analyze(ctx))
                .Where(static m => m != null);

            // sidecar metadata(.cs.meta) を AdditionalFiles として読み、Stable ID の正にする
            IncrementalValuesProvider<string> metaTexts = context.AdditionalTextsProvider
                .Where(static t => t.Path.EndsWith(".cs.meta", StringComparison.OrdinalIgnoreCase))
                .Select(static (t, ct) => t.GetText(ct)?.ToString() ?? string.Empty);

            // EditorSync / ValidateOnly（CI）モード。診断の severity を分ける
            IncrementalValueProvider<bool> validateOnly = context.AnalyzerConfigOptionsProvider
                .Select(static (p, _) => IsValidateOnly(p));

            var combined = models.Collect().Combine(metaTexts.Collect()).Combine(validateOnly);
            context.RegisterSourceOutput(combined, static (spc, data) =>
                Emit(spc, data.Left.Left, data.Left.Right, data.Right));
        }

        private static bool IsValidateOnly(AnalyzerConfigOptionsProvider provider)
        {
            return provider.GlobalOptions.TryGetValue("build_property.NEMScriptMetadataMode", out string? mode)
                && string.Equals(mode, "ValidateOnly", StringComparison.OrdinalIgnoreCase);
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

        private static void Emit(SourceProductionContext spc, ImmutableArray<ScriptTypeModel?> items,
            ImmutableArray<string> metaContents, bool validateOnly)
        {
            var models = items.Where(m => m != null).Select(m => m!).ToList();
            ScriptMetaIndex meta = ScriptMetaIndex.Build(metaContents);

            // ID 解決の優先順: 明示属性 → sidecar metadata → 決定的 fallback。
            foreach (ScriptTypeModel model in models)
            {
                bool hasStable;
                if (model.HasExplicitId && !model.InvalidId)
                {
                    // model.NormalizedId は attribute 由来で確定済み
                    hasStable = true;
                }
                else if (meta.TryGetScriptId(model.FullTypeName, out string metaId) &&
                    TryNormalizeGuid(metaId, out string normalizedMeta))
                {
                    model.NormalizedId = normalizedMeta;
                    hasStable = true;
                }
                else
                {
                    // metadata も attribute も無い（CI で sync 未実行など）。決定的 fallback
                    hasStable = false;
                }
                model.HasStableId = hasStable;
            }

            // 診断: 不正 attribute / stable id 欠落 / 重複
            foreach (ScriptTypeModel model in models)
            {
                if (model.HasExplicitId && model.InvalidId)
                {
                    spc.ReportDiagnostic(Diagnostic.Create(InvalidIdRule, model.Location, model.FullTypeName, model.RawId));
                }
                else if (!model.HasStableId)
                {
                    spc.ReportDiagnostic(Diagnostic.Create(
                        validateOnly ? MissingIdValidateRule : MissingIdRule, model.Location, model.FullTypeName));
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
                builder.Append("                new global::NEMEngine.ScriptTypeDescriptor(");
                builder.Append(EscapeString(model.NormalizedId)).Append(", ");
                builder.Append(EscapeString(model.FullTypeName)).Append(", ");
                builder.Append(EscapeString(model.DisplayName)).Append(", ");
                builder.Append(EscapeString(model.SourcePath)).Append(", ");
                builder.Append(model.HasStableId ? "true" : "false");
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
