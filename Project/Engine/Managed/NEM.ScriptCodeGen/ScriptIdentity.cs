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
using static NEM.ScriptCodeGen.ScriptSchemaText;

namespace NEM.ScriptCodeGen
{
    // スクリプト生成のID変換
    internal static class ScriptIdentity
    {
        internal static bool TryNormalizeGuid(string raw, out string normalized)
        {
            if (Guid.TryParse(raw, out Guid guid))
            {
                normalized = guid.ToString("D");
                return true;
            }
            normalized = string.Empty;
            return false;
        }

        internal static string DeterministicGuid(string seed)
        {
            using var md5 = MD5.Create();
            byte[] hash = md5.ComputeHash(Encoding.UTF8.GetBytes(seed));
            return new Guid(hash).ToString("D");
        }
    }
}
