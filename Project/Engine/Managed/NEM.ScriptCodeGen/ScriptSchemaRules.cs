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

using static NEM.ScriptCodeGen.ScriptSchemaAnalysis;
using static NEM.ScriptCodeGen.ScriptFieldAttributes;
using static NEM.ScriptCodeGen.ScriptKindResolver;
using static NEM.ScriptCodeGen.ScriptIdentity;
using static NEM.ScriptCodeGen.ScriptSchemaText;

namespace NEM.ScriptCodeGen
{
    // スクリプト生成の定数と診断
    internal static class ScriptSchemaRules
    {
        internal const string MonoBehaviourFullName = "NEMEngine.MonoBehaviour";
        internal const string AssetFullName = "NEMEngine.Asset";
        internal const string ComponentFullName = "NEMEngine.Component";
        internal const string ScriptTypeIDAttributeName = "NEMEngine.ScriptTypeIDAttribute";
        internal const string SerializeFieldAttributeName = "NEMEngine.SerializeFieldAttribute";
        internal const string SerializedFieldIDAttributeName = "NEMEngine.SerializedFieldIDAttribute";
        internal const string FormerlySerializedAsAttributeName = "NEMEngine.FormerlySerializedAsAttribute";
        internal const string HideInInspectorAttributeName = "NEMEngine.HideInInspectorAttribute";
        internal const string RangeAttributeName = "NEMEngine.RangeAttribute";
        internal const string MinAttributeName = "NEMEngine.MinAttribute";
        internal const string DragSpeedAttributeName = "NEMEngine.DragSpeedAttribute";
        internal const string ReadOnlyAttributeName = "NEMEngine.ReadOnlyAttribute";
        internal const string MultilineAttributeName = "NEMEngine.MultilineAttribute";
        internal const string SeparatorTextAttributeName = "NEMEngine.SeparatorTextAttribute";
        internal const string LabelAttributeName = "NEMEngine.LabelAttribute";
        internal const string TooltipAttributeName = "NEMEngine.TooltipAttribute";
        internal const string NativeAssetTypeAttributeName = "NEMEngine.NativeAssetTypeAttribute";
        internal const string SerializeReferenceAttributeName = "NEMEngine.SerializeReferenceAttribute";
        internal const string SerializableAttributeName = "System.SerializableAttribute";

        // schema 全体のバージョン。保存形式 schemaVersion と揃える
        internal const int SchemaVersion = 2;

        // [Serializable]型のメンバ展開の深さ上限。自己参照型はここで打ち切る(Unityの入れ子上限と同等)
        internal const int MaxObjectDepth = 7;

        internal static readonly DiagnosticDescriptor MissingFieldIDRule = new DiagnosticDescriptor(
            "NEMSG010",
            "Serialized field has no stable id",
            "Serialized field '{0}.{1}' has no stable Field GUID (no [SerializedFieldID] and no .cs.meta entry); a deterministic fallback GUID was generated. Run Editor metadata sync.",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor MissingFieldIDValidateRule = new DiagnosticDescriptor(
            "NEMSG014",
            "Serialized field metadata is missing",
            "Serialized field '{0}.{1}' has no stable Field GUID and metadata mode is ValidateOnly. Run Editor metadata sync to generate .cs.meta.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor InvalidFieldIDRule = new DiagnosticDescriptor(
            "NEMSG011",
            "Invalid [SerializedFieldID] value",
            "Serialized field '{0}.{1}' has an invalid [SerializedFieldID] value '{2}'. It must be a GUID.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor DuplicateFieldIDRule = new DiagnosticDescriptor(
            "NEMSG012",
            "Duplicate Serialized Field GUID",
            "Script type '{0}' has duplicate Serialized Field GUID '{1}' on fields '{2}' and '{3}'.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor UnsupportedFieldRule = new DiagnosticDescriptor(
            "NEMSG013",
            "Unsupported serialized field type",
            "Serialized field '{0}.{1}' has unsupported type '{2}' and will be skipped. Use a supported scalar, enum, math type, asset/GameObject/component/MonoBehaviour reference, array, List, or Nullable.",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor MissingSerializeReferenceRule = new DiagnosticDescriptor(
            "NEMSG015",
            "Polymorphic field requires SerializeReference",
            "シリアライズ対象 '{0}.{1}' は派生型を保持できる型 '{2}' です。実際の派生型を保持するには [SerializeReference] を指定してください。",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);


    }
}
