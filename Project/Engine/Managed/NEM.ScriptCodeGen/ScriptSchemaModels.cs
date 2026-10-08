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
        internal sealed class TypeSchema
        {
            public string ScriptTypeID = string.Empty;       // Analyze 時点では attr or 決定的（Emit で meta 上書き）
            public bool HasExplicitScriptID;                  // [ScriptTypeID] が明示・有効か
            public string FullTypeName = string.Empty;
            public List<FieldSchema> Fields = new List<FieldSchema>();
        }

        internal sealed class FieldSchema
        {
            public string FieldID = string.Empty;
            public string Name = string.Empty;
            public string DeclaringType = string.Empty;
            public List<string> FormerNames = new List<string>();
            public KindInfo Kind = new KindInfo();
            public bool IsPublic;
            public bool IsReadOnly;
            public bool IsHidden;
            public bool HasRange;
            public float RangeMin;
            public float RangeMax;
            public bool HasMin;
            public float MinValue;
            public bool HasDragSpeed;
            public float DragSpeed;
            public string? Tooltip;
            public string? Header;
            public string? Label;
            public bool Multiline;
            public bool RawIDInvalid;
            public string RawID = string.Empty;
            public string DeclaredType = string.Empty;
            public bool MissingSerializeReference;
            public Location Location = Location.None;
        }

        // 再帰的な値種別。collection / nullable は Element を、Object はメンバを、ManagedReference は候補型を持つ
        internal sealed class KindInfo
        {
            public string Kind = "Unsupported";
            public KindInfo? Element;
            public string? EnumUnderlying;
            public List<string> EnumNames = new List<string>();
            public List<string> EnumValues = new List<string>();
            public string? AssetType;
            public string? ScriptType;
            public string? ComponentType;
            public string? ObjectType;
            public List<FieldSchema> Members = new List<FieldSchema>();
            public List<KindInfo> Candidates = new List<KindInfo>();
        }

}
