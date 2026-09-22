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
    // Manifest生成の入力を接続する
    [Generator(LanguageNames.CSharp)]
    public sealed class ScriptManifestGenerator : IIncrementalGenerator
    {
        public void Initialize(IncrementalGeneratorInitializationContext context)
        {
            IncrementalValuesProvider<ScriptTypeModel?> models = context.SyntaxProvider.CreateSyntaxProvider(
                static (node, _) => node is ClassDeclarationSyntax c && c.BaseList != null,
                static (ctx, _) => ScriptManifestAnalysis.Analyze(ctx))
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
                ScriptManifestEmitter.Emit(spc, data.Left.Left, data.Left.Right, data.Right));
        }

        internal static bool IsValidateOnly(AnalyzerConfigOptionsProvider provider)
        {
            return provider.GlobalOptions.TryGetValue("build_property.NEMScriptMetadataMode", out string? mode)
                && string.Equals(mode, "ValidateOnly", StringComparison.OrdinalIgnoreCase);
        }
    }
}
