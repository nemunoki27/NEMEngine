using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Microsoft.CodeAnalysis.CSharp.Syntax;

namespace NEM.ScriptMetaSync;

// スクリプトmetaのソース解析
internal static class ScriptSourceCollector {

    private const string ScriptBehaviourName = "ScriptBehaviour";

    internal static Dictionary<string, List<ScriptModel>> Collect(string root) {
        // .cs を収集（生成物・VCS・reload 作業領域は除外）
        var csFiles = new List<string>();
        foreach (string file in Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)) {
            if (IsExcluded(file)) { continue; }
            csFiles.Add(file);
        }

        // 全 class の継承マップ（transitive に ScriptBehaviour 派生を判定するため）
        var classMap = new Dictionary<string, ClassInfo>(StringComparer.Ordinal);
        var perFile = new Dictionary<string, List<ScriptModel>>(StringComparer.OrdinalIgnoreCase);
        var parsedRoots = new Dictionary<string, CompilationUnitSyntax>(StringComparer.OrdinalIgnoreCase);

        foreach (string file in csFiles) {
            string text = File.ReadAllText(file);
            SyntaxTree tree = CSharpSyntaxTree.ParseText(text);
            var unit = (CompilationUnitSyntax)tree.GetRoot();
            parsedRoots[file] = unit;
            foreach (ClassDeclarationSyntax cls in unit.DescendantNodes().OfType<ClassDeclarationSyntax>()) {
                ClassInfo info = new() {
                    SimpleName = cls.Identifier.Text,
                    BaseSimpleName = FirstBaseSimpleName(cls),
                    IsAbstract = cls.Modifiers.Any(m => m.IsKind(SyntaxKind.AbstractKeyword)),
                };
                classMap[info.SimpleName] = info;
            }
        }

        // 各ファイルから script 型を抽出する
        foreach (string file in csFiles) {
            var models = ExtractScripts(parsedRoots[file], classMap);
            if (models.Count > 0) {
                perFile[file] = models;
            }
        }

        return perFile;
    }

    internal static bool IsExcluded(string path) {
        string norm = path.Replace('\\', '/');
        string[] excluded = { "/bin/", "/obj/", "/Generated/", "/Staging/", "/Shadow/", "/LastKnownGood/", "/.git/", "/.vs/" };
        foreach (string e in excluded) {
            if (norm.Contains(e, StringComparison.OrdinalIgnoreCase)) { return true; }
        }
        return norm.EndsWith(".g.cs", StringComparison.OrdinalIgnoreCase)
            || norm.EndsWith(".generated.cs", StringComparison.OrdinalIgnoreCase);
    }

    internal static string? FirstBaseSimpleName(ClassDeclarationSyntax cls) {
        if (cls.BaseList == null) { return null; }
        foreach (BaseTypeSyntax baseType in cls.BaseList.Types) {
            // 最初の base を基底クラス候補とする（interface でも simple 名で照合する）
            return SimpleNameOf(baseType.Type);
        }
        return null;
    }

    internal static string SimpleNameOf(TypeSyntax type) {
        return type switch {
            IdentifierNameSyntax id => id.Identifier.Text,
            QualifiedNameSyntax q => q.Right.Identifier.Text,
            GenericNameSyntax g => g.Identifier.Text,
            _ => type.ToString(),
        };
    }

    internal static bool DerivesFromScriptBehaviour(string? simpleName, Dictionary<string, ClassInfo> map) {
        int guard = 0;
        string? current = simpleName;
        while (!string.IsNullOrEmpty(current) && guard++ < 64) {
            if (current == ScriptBehaviourName) { return true; }
            if (!map.TryGetValue(current!, out ClassInfo? info)) { return false; }
            current = info.BaseSimpleName;
        }
        return false;
    }

    internal static List<ScriptModel> ExtractScripts(CompilationUnitSyntax unit, Dictionary<string, ClassInfo> map) {

        var result = new List<ScriptModel>();
        foreach (ClassDeclarationSyntax cls in unit.DescendantNodes().OfType<ClassDeclarationSyntax>()) {

            if (cls.Modifiers.Any(m => m.IsKind(SyntaxKind.AbstractKeyword))) { continue; }
            if (!DerivesFromScriptBehaviour(FirstBaseSimpleName(cls), map)) { continue; }

            string ns = NamespaceOf(cls);
            var model = new ScriptModel {
                FullTypeName = string.IsNullOrEmpty(ns) ? cls.Identifier.Text : ns + "." + cls.Identifier.Text,
            };
            model.ExplicitID = AttributeStringArg(cls.AttributeLists, "ScriptTypeID");
            model.FormerlyKnown = AttributeStringArgs(cls.AttributeLists, "FormerlyKnownScriptType");

            foreach (FieldDeclarationSyntax field in cls.Members.OfType<FieldDeclarationSyntax>()) {

                if (!IsSerializedField(field)) { continue; }
                string typeText = field.Declaration.Type.ToString();
                string? explicitID = AttributeStringArg(field.AttributeLists, "SerializedFieldID");
                List<string> formerly = AttributeStringArgs(field.AttributeLists, "FormerlySerializedAs");

                foreach (VariableDeclaratorSyntax v in field.Declaration.Variables) {
                    model.Fields.Add(new FieldModel {
                        Name = v.Identifier.Text,
                        TypeText = typeText,
                        ExplicitID = explicitID,
                        FormerlySerializedAs = new List<string>(formerly),
                    });
                }
            }
            result.Add(model);
        }
        return result;
    }

    internal static string NamespaceOf(SyntaxNode node) {
        for (SyntaxNode? n = node.Parent; n != null; n = n.Parent) {
            if (n is FileScopedNamespaceDeclarationSyntax fs) { return fs.Name.ToString(); }
            if (n is NamespaceDeclarationSyntax ns) { return ns.Name.ToString(); }
        }
        return string.Empty;
    }

    internal static bool IsSerializedField(FieldDeclarationSyntax field) {
        bool isStatic = field.Modifiers.Any(m => m.IsKind(SyntaxKind.StaticKeyword));
        bool isConst = field.Modifiers.Any(m => m.IsKind(SyntaxKind.ConstKeyword));
        bool isReadonly = field.Modifiers.Any(m => m.IsKind(SyntaxKind.ReadOnlyKeyword));
        if (isStatic || isConst || isReadonly) { return false; }
        bool isPublic = field.Modifiers.Any(m => m.IsKind(SyntaxKind.PublicKeyword));
        bool hasSerializeField = HasAttribute(field.AttributeLists, "SerializeField");
        return isPublic || hasSerializeField;
    }

    internal static bool HasAttribute(SyntaxList<AttributeListSyntax> lists, string name) {
        foreach (AttributeListSyntax list in lists) {
            foreach (AttributeSyntax attr in list.Attributes) {
                if (MatchesAttributeName(attr, name)) { return true; }
            }
        }
        return false;
    }

    internal static string? AttributeStringArg(SyntaxList<AttributeListSyntax> lists, string name) {
        foreach (AttributeListSyntax list in lists) {
            foreach (AttributeSyntax attr in list.Attributes) {
                if (MatchesAttributeName(attr, name) && attr.ArgumentList?.Arguments.Count >= 1) {
                    return LiteralString(attr.ArgumentList.Arguments[0].Expression);
                }
            }
        }
        return null;
    }

    internal static List<string> AttributeStringArgs(SyntaxList<AttributeListSyntax> lists, string name) {
        var values = new List<string>();
        foreach (AttributeListSyntax list in lists) {
            foreach (AttributeSyntax attr in list.Attributes) {
                if (MatchesAttributeName(attr, name) && attr.ArgumentList?.Arguments.Count >= 1) {
                    string? s = LiteralString(attr.ArgumentList.Arguments[0].Expression);
                    if (!string.IsNullOrWhiteSpace(s)) { values.Add(s!); }
                }
            }
        }
        return values;
    }

    internal static bool MatchesAttributeName(AttributeSyntax attr, string name) {
        string text = SimpleNameOf(attr.Name);
        return text == name || text == name + "Attribute";
    }

    internal static string? LiteralString(ExpressionSyntax expr) {
        return expr is LiteralExpressionSyntax lit && lit.IsKind(SyntaxKind.StringLiteralExpression)
            ? lit.Token.ValueText : null;
    }
}
