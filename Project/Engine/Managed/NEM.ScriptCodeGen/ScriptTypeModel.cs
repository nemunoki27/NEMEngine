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
        internal sealed class ScriptTypeModel
        {
            public string FullTypeName = string.Empty;
            public string DisplayName = string.Empty;
            public string SourcePath = string.Empty;
            public string RawID = string.Empty;
            public bool HasExplicitID;
            public string NormalizedID = string.Empty;
            public bool InvalidID;
            // 明示属性 or sidecar metadata で安定 ID が得られたか（false は決定的 fallback）
            public bool HasStableID;
            public Location Location = Location.None;
        }
}
