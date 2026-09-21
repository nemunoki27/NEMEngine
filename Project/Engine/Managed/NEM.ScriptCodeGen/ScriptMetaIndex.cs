using System;
using System.Collections.Generic;
using System.Collections.Immutable;

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

        public static ScriptMetaIndex Build(ImmutableArray<string> metaContents)
        {
            var index = new ScriptMetaIndex();
            foreach (string content in metaContents)
            {
                if (string.IsNullOrEmpty(content)) { continue; }
                if (MetaJson.Parse(content) is not Dictionary<string, object?> root) { continue; }
                if (!root.TryGetValue("scripts", out object? scriptsObj) || scriptsObj is not List<object?> scripts) { continue; }

                foreach (object? scriptObj in scripts)
                {
                    if (scriptObj is not Dictionary<string, object?> script) { continue; }
                    string fullName = AsString(script, "fullTypeName");
                    string scriptID = AsString(script, "scriptTypeId");
                    if (string.IsNullOrEmpty(fullName) || string.IsNullOrEmpty(scriptID)) { continue; }

                    var meta = new ScriptMeta { ScriptTypeID = scriptID };
                    meta.FormerNames = AsStringList(script, "formerNames");

                    if (script.TryGetValue("fields", out object? fieldsObj) && fieldsObj is List<object?> fields)
                    {
                        foreach (object? fieldObj in fields)
                        {
                            if (fieldObj is not Dictionary<string, object?> field) { continue; }
                            string name = AsString(field, "name");
                            string fieldID = AsString(field, "fieldId");
                            if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(fieldID)) { continue; }

                            meta.FieldIDByName[name] = fieldID;
                            List<string> formers = AsStringList(field, "formerNames");
                            meta.FieldFormerNamesByName[name] = formers;
                            // rename後も同じfieldIDを引けるようにする
                            foreach (string former in formers)
                            {
                                if (!meta.FieldIDByName.ContainsKey(former)) { meta.FieldIDByName[former] = fieldID; }
                            }
                        }
                    }
                    index.byFullName[fullName] = meta;
                }
            }
            return index;
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
            if (obj.TryGetValue(key, out object? value) && value is List<object?> list)
            {
                foreach (object? item in list)
                {
                    if (item is string s && !string.IsNullOrEmpty(s)) { result.Add(s); }
                }
            }
            return result;
        }
    }
}
