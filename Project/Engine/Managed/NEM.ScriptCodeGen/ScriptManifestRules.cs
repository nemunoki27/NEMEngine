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
    internal static class ScriptManifestRules
    {
        internal const string ScriptBehaviourFullName = "NEMEngine.ScriptBehaviour";
        internal const string ScriptTypeIDAttributeName = "NEMEngine.ScriptTypeIDAttribute";

        internal static readonly DiagnosticDescriptor MissingIDRule = new DiagnosticDescriptor(
            "NEMSG001",
            "Script type has no stable id",
            "Script type '{0}' has no stable Script Type GUID (no [ScriptTypeID] and no .cs.meta entry); a temporary fallback GUID was generated. Run Editor metadata sync.",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor MissingIDValidateRule = new DiagnosticDescriptor(
            "NEMSG004",
            "Script type metadata is missing",
            "Script type '{0}' has no stable Script Type GUID and metadata mode is ValidateOnly. Run Editor metadata sync to generate .cs.meta.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor InvalidIDRule = new DiagnosticDescriptor(
            "NEMSG002",
            "Invalid [ScriptTypeID] value",
            "Script type '{0}' has an invalid [ScriptTypeID] value '{1}'. It must be a GUID. A fallback GUID was generated.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        internal static readonly DiagnosticDescriptor DuplicateIDRule = new DiagnosticDescriptor(
            "NEMSG003",
            "Duplicate Script Type GUID",
            "Script types '{0}' and '{1}' share the same Script Type GUID '{2}'. GUIDs must be unique.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

    }
}
