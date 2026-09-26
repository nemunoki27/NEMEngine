using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptMetaSync;

    internal enum Mode { EditorSync, ValidateOnly }

    // 検出した 1 フィールド
    internal sealed class FieldModel {
        public string Name = string.Empty;
        public string TypeText = string.Empty;
        public string? ExplicitID;            // [SerializedFieldID]
        public List<string> FormerlySerializedAs = new();
    }

    // 検出した 1 script 型
    internal sealed class ScriptModel {
        public string FullTypeName = string.Empty;
        public string? ExplicitID;            // [ScriptTypeID]
        public List<string> FormerlyKnown = new();   // [FormerlyKnownScriptType]
        public List<FieldModel> Fields = new();
    }

