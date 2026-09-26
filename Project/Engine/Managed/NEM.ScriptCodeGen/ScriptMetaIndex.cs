using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using Microsoft.CodeAnalysis;

namespace NEM.ScriptCodeGen
{
    // .cs.meta（sidecar metadata）を Stable ID の正として読み取るインデックス。
    // 生成器は「明示属性 → sidecar metadata → 決定的 fallback」の優先順で ID を解決する。
    internal sealed class ScriptMetaIndex
    {
        private sealed class ScriptMeta
        {
            public string ScriptTypeID = string.Empty;
            public List<string> FormerNames = new List<string>();
            // 現在名と formerNames の両方を key にして fieldID を引けるようにする
            public Dictionary<string, string> FieldIDByName = new Dictionary<string, string>(StringComparer.Ordinal);
            public Dictionary<string, List<string>> FieldFormerNamesByName = new Dictionary<string, List<string>>(StringComparer.Ordinal);
        }

        private readonly Dictionary<string, ScriptMeta> byFullName = new Dictionary<string, ScriptMeta>(StringComparer.Ordinal);

        private static readonly DiagnosticDescriptor InvalidMetadata = new DiagnosticDescriptor(
            "NEMSG016", "Invalid script metadata", "Script metadata is invalid: {0}",
            "NEMEngine", DiagnosticSeverity.Error, true);

        // 壊れたmetaでは生成を止め、別IDへの置き換えを防ぐ
        internal static ScriptMetaIndex? Read(SourceProductionContext context, ImmutableArray<string> contents) {
            try { return Build(contents); }
            catch (FormatException exception) {
                context.ReportDiagnostic(Diagnostic.Create(InvalidMetadata, Location.None, exception.Message));
                return null;
            }
        }

        public static ScriptMetaIndex Build(ImmutableArray<string> metaContents)
        {
            var index = new ScriptMetaIndex();
            var scriptIDs = new HashSet<string>(StringComparer.Ordinal);
            foreach (string content in metaContents)
            {
                if (MetaJson.Parse(content) is not Dictionary<string, object?> root) { throw new FormatException("Expected metadata object."); }
                if (!root.TryGetValue("scripts", out object? scriptsObj)) { continue; }
                if (scriptsObj is not List<object?> scripts) { throw new FormatException("Expected scripts array."); }

                foreach (object? scriptObj in scripts)
                {
                    if (scriptObj is not Dictionary<string, object?> script) { throw new FormatException("Invalid script entry."); }
                    string fullName = AsString(script, "fullTypeName");
                    string scriptID = AsString(script, "scriptTypeId");
                    if (string.IsNullOrWhiteSpace(fullName) || !ScriptIdentity.TryNormalizeGuid(scriptID, out scriptID)) {
                        throw new FormatException("Invalid script name or ID: " + fullName);
                    }
                    if (index.byFullName.ContainsKey(fullName) || !scriptIDs.Add(scriptID)) {
                        throw new FormatException("Duplicate script name or ID: " + fullName);
                    }

                    var meta = new ScriptMeta { ScriptTypeID = scriptID };
                    meta.FormerNames = AsStringList(script, "formerNames");

                    if (script.TryGetValue("fields", out object? fieldsObj))
                    {
                        if (fieldsObj is not List<object?> fields) { throw new FormatException("Expected fields array: " + fullName); }
                        var fieldIDs = new HashSet<string>(StringComparer.Ordinal);
                        var fieldNames = new HashSet<string>(StringComparer.Ordinal);
                        foreach (object? fieldObj in fields)
                        {
                            if (fieldObj is not Dictionary<string, object?> field) { throw new FormatException("Invalid field entry: " + fullName); }
                            string name = AsString(field, "name");
                            string fieldID = AsString(field, "fieldId");
                            if (string.IsNullOrWhiteSpace(name) || !ScriptIdentity.TryNormalizeGuid(fieldID, out fieldID)) {
                                throw new FormatException("Invalid field name or ID: " + fullName + "." + name);
                            }
                            if (!fieldIDs.Add(fieldID) || !fieldNames.Add(name)) {
                                throw new FormatException("Duplicate field name or ID: " + fullName + "." + name);
                            }

                            AddFieldName(meta, name, fieldID);
                            List<string> formers = AsStringList(field, "formerNames");
                            meta.FieldFormerNamesByName[name] = formers;
                            // rename後も同じfieldIDを引けるようにする
                            foreach (string former in formers)
                            {
                                AddFieldName(meta, former, fieldID);
                            }
                        }
                    }
                    index.byFullName[fullName] = meta;
                }
            }
            return index;
        }

        private static void AddFieldName(ScriptMeta meta, string name, string fieldID) {
            if (meta.FieldIDByName.TryGetValue(name, out string? previous) && previous != fieldID) {
                throw new FormatException("Ambiguous former field name: " + name);
            }
            meta.FieldIDByName[name] = fieldID;
        }

        public bool TryGetScriptID(string fullTypeName, out string scriptTypeID)
        {
            if (byFullName.TryGetValue(fullTypeName, out ScriptMeta? meta))
            {
                scriptTypeID = meta.ScriptTypeID;
                return true;
            }
            scriptTypeID = string.Empty;
            return false;
        }

        public List<string> GetScriptFormerNames(string fullTypeName)
        {
            return byFullName.TryGetValue(fullTypeName, out ScriptMeta? meta) ? meta.FormerNames : new List<string>();
        }

        public bool TryGetFieldID(string fullTypeName, string fieldName, out string fieldID)
        {
            if (byFullName.TryGetValue(fullTypeName, out ScriptMeta? meta) &&
                meta.FieldIDByName.TryGetValue(fieldName, out string? id))
            {
                fieldID = id;
                return true;
            }
            fieldID = string.Empty;
            return false;
        }

        public List<string> GetFieldFormerNames(string fullTypeName, string fieldName)
        {
            if (byFullName.TryGetValue(fullTypeName, out ScriptMeta? meta) &&
                meta.FieldFormerNamesByName.TryGetValue(fieldName, out List<string>? formers))
            {
                return formers;
            }
            return new List<string>();
        }

        private static string AsString(Dictionary<string, object?> obj, string key)
        {
            return obj.TryGetValue(key, out object? value) && value is string s ? s : string.Empty;
        }

        private static List<string> AsStringList(Dictionary<string, object?> obj, string key)
        {
            var result = new List<string>();
            if (obj.TryGetValue(key, out object? value))
            {
                if (value is not List<object?> list) { throw new FormatException("Expected string array: " + key); }
                foreach (object? item in list)
                {
                    if (item is not string s || string.IsNullOrWhiteSpace(s)) { throw new FormatException("Invalid former name."); }
                    result.Add(s);
                }
            }
            return result;
        }
    }
}
