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
using static NEM.ScriptCodeGen.ScriptKindResolver;
using static NEM.ScriptCodeGen.ScriptIdentity;
using static NEM.ScriptCodeGen.ScriptSchemaText;

namespace NEM.ScriptCodeGen
{
    // スクリプト生成の処理
    internal static class ScriptSchemaIdentity
    {
        internal static void Resolve(SourceProductionContext spc, List<TypeSchema> schemas, ScriptMetaIndex meta, bool validateOnly)
        {
            // ID 解決の優先順: 明示属性 → sidecar metadata → 決定的 fallback
            foreach (TypeSchema type in schemas)
            {
                if (!type.HasExplicitScriptID && meta.TryGetScriptID(type.FullTypeName, out string metaScriptID) &&
                    TryNormalizeGuid(metaScriptID, out string normalizedScriptID))
                {
                    type.ScriptTypeID = normalizedScriptID;
                }
                foreach (FieldSchema field in type.Fields)
                {
                    bool explicitField = !string.IsNullOrWhiteSpace(field.RawID) && !field.RawIDInvalid;
                    if (!explicitField && meta.TryGetFieldID(type.FullTypeName, field.Name, out string metaFieldID) &&
                        TryNormalizeGuid(metaFieldID, out string normalizedFieldID))
                    {
                        field.FieldID = normalizedFieldID;
                    }
                    foreach (string former in meta.GetFieldFormerNames(type.FullTypeName, field.Name))
                    {
                        if (former != field.Name && !field.FormerNames.Contains(former)) { field.FormerNames.Add(former); }
                    }
                }
            }

            foreach (TypeSchema type in schemas)
            {
                var seen = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (FieldSchema field in type.Fields)
                {
                    bool explicitField = !string.IsNullOrWhiteSpace(field.RawID) && !field.RawIDInvalid;
                    bool fromMeta = meta.TryGetFieldID(type.FullTypeName, field.Name, out _);
                    if (field.RawIDInvalid)
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(InvalidFieldIDRule, field.Location, type.FullTypeName, field.Name, field.RawID));
                    }
                    else if (!explicitField && !fromMeta)
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(
                            validateOnly ? MissingFieldIDValidateRule : MissingFieldIDRule, field.Location, type.FullTypeName, field.Name));
                    }
                    if (field.Kind.Kind == "Unsupported")
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(UnsupportedFieldRule, field.Location, type.FullTypeName, field.Name, "unsupported"));
                    }
                    if (field.MissingSerializeReference)
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(MissingSerializeReferenceRule, field.Location,
                            type.FullTypeName, field.Name, field.DeclaredType));
                    }
                    if (seen.TryGetValue(field.FieldID, out string? otherName))
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(DuplicateFieldIDRule, field.Location, type.FullTypeName, field.FieldID, otherName, field.Name));
                    }
                    else
                    {
                        seen[field.FieldID] = field.Name;
                    }
                }
            }

        }
    }
}
