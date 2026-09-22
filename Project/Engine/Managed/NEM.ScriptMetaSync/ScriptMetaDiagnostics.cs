using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptMetaSync;

// スクリプトmetaの診断
internal sealed class ScriptMetaDiagnostics {

    internal int errorCount;
    internal int ambiguousCount;

    internal string? NormalizeOrError(string? raw, string context) {
        if (string.IsNullOrWhiteSpace(raw)) { return null; }
        if (Guid.TryParse(raw, out Guid guid)) { return guid.ToString("D"); }
        errorCount++;
        Console.Error.WriteLine($"[ScriptMetaSync] invalid GUID '{raw}' ({context}).");
        return null;
    }
}
