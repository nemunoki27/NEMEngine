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
            public string ScriptTypeId = string.Empty;
            public List<string> FormerNames = new List<string>();
            // 現在名と formerNames の両方を key にして fieldId を引けるようにする
            public Dictionary<string, string> FieldIdByName = new Dictionary<string, string>(StringComparer.Ordinal);
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
                    string scriptId = AsString(script, "scriptTypeId");
                    if (string.IsNullOrEmpty(fullName) || string.IsNullOrEmpty(scriptId)) { continue; }

                    var meta = new ScriptMeta { ScriptTypeId = scriptId };
                    meta.FormerNames = AsStringList(script, "formerNames");

                    if (script.TryGetValue("fields", out object? fieldsObj) && fieldsObj is List<object?> fields)
                    {
                        foreach (object? fieldObj in fields)
                        {
                            if (fieldObj is not Dictionary<string, object?> field) { continue; }
                            string name = AsString(field, "name");
                            string fieldId = AsString(field, "fieldId");
                            if (string.IsNullOrEmpty(name) || string.IsNullOrEmpty(fieldId)) { continue; }

                            meta.FieldIdByName[name] = fieldId;
                            List<string> formers = AsStringList(field, "formerNames");
                            meta.FieldFormerNamesByName[name] = formers;
                            // 旧名でも fieldId を引けるようにする（rename 後の legacy data 解決）
                            foreach (string former in formers)
                            {
                                if (!meta.FieldIdByName.ContainsKey(former)) { meta.FieldIdByName[former] = fieldId; }
                            }
                        }
                    }
                    index.byFullName[fullName] = meta;
                }
            }
            return index;
        }

        public bool TryGetScriptId(string fullTypeName, out string scriptTypeId)
        {
            if (byFullName.TryGetValue(fullTypeName, out ScriptMeta? meta))
            {
                scriptTypeId = meta.ScriptTypeId;
                return true;
            }
            scriptTypeId = string.Empty;
            return false;
        }

        public List<string> GetScriptFormerNames(string fullTypeName)
        {
            return byFullName.TryGetValue(fullTypeName, out ScriptMeta? meta) ? meta.FormerNames : new List<string>();
        }

        public bool TryGetFieldId(string fullTypeName, string fieldName, out string fieldId)
        {
            if (byFullName.TryGetValue(fullTypeName, out ScriptMeta? meta) &&
                meta.FieldIdByName.TryGetValue(fieldName, out string? id))
            {
                fieldId = id;
                return true;
            }
            fieldId = string.Empty;
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
