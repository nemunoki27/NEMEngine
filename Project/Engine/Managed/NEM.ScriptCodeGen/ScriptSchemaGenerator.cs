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
    // スキーマ生成の入力を接続する
    [Generator(LanguageNames.CSharp)]
    public sealed class ScriptSchemaGenerator : IIncrementalGenerator
    {
        public void Initialize(IncrementalGeneratorInitializationContext context)
        {
            IncrementalValuesProvider<TypeSchema?> schemas = context.SyntaxProvider.CreateSyntaxProvider(
                static (node, _) => node is ClassDeclarationSyntax,
                static (ctx, _) => ScriptSchemaAnalysis.Analyze(ctx))
                .Where(static s => s != null);

            // sidecar metadata(.cs.meta) を AdditionalFiles として読み Stable ID の正にする
            IncrementalValuesProvider<string> metaTexts = context.AdditionalTextsProvider
                .Where(static t => t.Path.EndsWith(".cs.meta", StringComparison.OrdinalIgnoreCase))
                .Select(static (t, ct) => t.GetText(ct)?.ToString() ?? string.Empty);

            IncrementalValueProvider<bool> validateOnly = context.AnalyzerConfigOptionsProvider
                .Select(static (p, _) => p.GlobalOptions.TryGetValue("build_property.NEMScriptMetadataMode", out string? mode)
                    && string.Equals(mode, "ValidateOnly", StringComparison.OrdinalIgnoreCase));

            var combined = schemas.Collect().Combine(metaTexts.Collect()).Combine(validateOnly);
            context.RegisterSourceOutput(combined, static (spc, data) =>
                ScriptSchemaEmitter.Emit(spc, data.Left.Left, data.Left.Right, data.Right));
        }
    }
}
