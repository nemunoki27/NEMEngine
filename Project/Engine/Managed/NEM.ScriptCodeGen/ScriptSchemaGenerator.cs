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
    // concrete ScriptBehaviour の serialized field schema を決定的に生成する。
    // reflection に依存せず、各 field の Stable Field GUID / 名前 / 旧名 / valueKind / enum / collection /
    // nullable / reference filter / Inspector 属性を compile 時に確定して JSON へ焼き込む。
    // HostBridge は load 時にこの JSON を読み、field 名で reflection FieldInfo を一度だけ突き合わせて
    // runtime get/set と default 値抽出に使う（hot path では parse / reflection しない）。
    [Generator(LanguageNames.CSharp)]
    public sealed class ScriptSchemaGenerator : IIncrementalGenerator
    {
        private const string ScriptBehaviourFullName = "NEMEngine.ScriptBehaviour";
        private const string AssetFullName = "NEMEngine.Asset";
        private const string ComponentFullName = "NEMEngine.Component";
        private const string ScriptTypeIdAttributeName = "NEMEngine.ScriptTypeIdAttribute";
        private const string SerializeFieldAttributeName = "NEMEngine.SerializeFieldAttribute";
        private const string SerializedFieldIdAttributeName = "NEMEngine.SerializedFieldIdAttribute";
        private const string FormerlySerializedAsAttributeName = "NEMEngine.FormerlySerializedAsAttribute";
        private const string HideInInspectorAttributeName = "NEMEngine.HideInInspectorAttribute";
        private const string RangeAttributeName = "NEMEngine.RangeAttribute";
        private const string MinAttributeName = "NEMEngine.MinAttribute";
        private const string DragSpeedAttributeName = "NEMEngine.DragSpeedAttribute";
        private const string ReadOnlyAttributeName = "NEMEngine.ReadOnlyAttribute";
        private const string MultilineAttributeName = "NEMEngine.MultilineAttribute";
        private const string SeparatorTextAttributeName = "NEMEngine.SeparatorTextAttribute";
        private const string LabelAttributeName = "NEMEngine.LabelAttribute";
        private const string TooltipAttributeName = "NEMEngine.TooltipAttribute";
        private const string NativeAssetTypeAttributeName = "NEMEngine.NativeAssetTypeAttribute";

        // schema 全体のバージョン。保存形式 schemaVersion と揃える
        private const int SchemaVersion = 2;

        private static readonly DiagnosticDescriptor MissingFieldIdRule = new DiagnosticDescriptor(
            "NEMSG010",
            "Serialized field has no stable id",
            "Serialized field '{0}.{1}' has no stable Field GUID (no [SerializedFieldId] and no .cs.meta entry); a deterministic fallback GUID was generated. Run Editor metadata sync.",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor MissingFieldIdValidateRule = new DiagnosticDescriptor(
            "NEMSG014",
            "Serialized field metadata is missing",
            "Serialized field '{0}.{1}' has no stable Field GUID and metadata mode is ValidateOnly. Run Editor metadata sync to generate .cs.meta.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor InvalidFieldIdRule = new DiagnosticDescriptor(
            "NEMSG011",
            "Invalid [SerializedFieldId] value",
            "Serialized field '{0}.{1}' has an invalid [SerializedFieldId] value '{2}'. It must be a GUID.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor DuplicateFieldIdRule = new DiagnosticDescriptor(
            "NEMSG012",
            "Duplicate Serialized Field GUID",
            "Script type '{0}' has duplicate Serialized Field GUID '{1}' on fields '{2}' and '{3}'.",
            "NEMScript", DiagnosticSeverity.Error, isEnabledByDefault: true);

        private static readonly DiagnosticDescriptor UnsupportedFieldRule = new DiagnosticDescriptor(
            "NEMSG013",
            "Unsupported serialized field type",
            "Serialized field '{0}.{1}' has unsupported type '{2}' and will be skipped. Use a supported scalar, enum, math type, asset/Entity/component/ScriptBehaviour reference, array, List, or Nullable.",
            "NEMScript", DiagnosticSeverity.Warning, isEnabledByDefault: true);

        private sealed class TypeSchema
        {
            public string ScriptTypeId = string.Empty;       // Analyze 時点では attr or 決定的（Emit で meta 上書き）
            public bool HasExplicitScriptId;                  // [ScriptTypeId] が明示・有効か
            public string FullTypeName = string.Empty;
            public List<FieldSchema> Fields = new List<FieldSchema>();
        }

        private sealed class FieldSchema
        {
            public string FieldId = string.Empty;
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
            public bool RawIdInvalid;
            public string RawId = string.Empty;
            public Location Location = Location.None;
        }

        // 再帰的な値種別。collection / nullable は Element を持つ
        private sealed class KindInfo
        {
            public string Kind = "Unsupported";
            public KindInfo? Element;
            public string? EnumUnderlying;
            public List<string> EnumNames = new List<string>();
            public List<string> EnumValues = new List<string>();
            public string? AssetType;
            public string? ScriptType;
            public string? ComponentType;
        }

        public void Initialize(IncrementalGeneratorInitializationContext context)
        {
            IncrementalValuesProvider<TypeSchema?> schemas = context.SyntaxProvider.CreateSyntaxProvider(
                static (node, _) => node is ClassDeclarationSyntax c && c.BaseList != null,
                static (ctx, _) => Analyze(ctx))
                .Where(static s => s != null);

            // sidecar metadata(.cs.meta) を AdditionalFiles として読み Stable ID の正にする
            IncrementalValuesProvider<string> metaTexts = context.AdditionalTextsProvider
                .Where(static t => t.Path.EndsWith(".cs.meta", StringComparison.OrdinalIgnoreCase))
                .Select(static (t, ct) => t.GetText(ct)?.ToString() ?? string.Empty);

            IncrementalValueProvider<bool> validateOnly = context.AnalyzerConfigOptionsProvider
                .Select(static (p, _) => p.GlobalOptions.TryGetValue("build_property.NEMScriptMetadataMode", out string? mode)
                    && string.Equals(mode, "ValidateOnly", StringComparison.OrdinalIgnoreCase));

            var combined = schemas.Collect().Combine(metaTexts.Collect()).Combine(validateOnly);
            context.RegisterSourceOutput(combined, static (spc, data) =>
                Emit(spc, data.Left.Left, data.Left.Right, data.Right));
        }

        private static TypeSchema? Analyze(GeneratorSyntaxContext ctx)
        {
            var classDecl = (ClassDeclarationSyntax)ctx.Node;
            if (ctx.SemanticModel.GetDeclaredSymbol(classDecl) is not INamedTypeSymbol symbol)
            {
                return null;
            }
            if (symbol.IsAbstract || !DerivesFromScriptBehaviour(symbol))
            {
                return null;
            }

            string fullName = symbol.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
                .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted));
            string scriptTypeId = ResolveScriptTypeId(symbol, fullName, out bool hasExplicitScriptId);

            var schema = new TypeSchema { ScriptTypeId = scriptTypeId, HasExplicitScriptId = hasExplicitScriptId, FullTypeName = fullName };

            // 継承を含めた field 集合（base から先に、宣言順）を集める
            var declaring = new List<INamedTypeSymbol>();
            for (INamedTypeSymbol? cur = symbol; cur != null && cur.ToDisplayString() != ScriptBehaviourFullName; cur = cur.BaseType)
            {
                declaring.Add(cur);
            }
            declaring.Reverse();

            foreach (INamedTypeSymbol owner in declaring)
            {
                string ownerName = owner.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
                    .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted));
                foreach (ISymbol member in owner.GetMembers())
                {
                    if (member is not IFieldSymbol field || field.IsImplicitlyDeclared)
                    {
                        continue;
                    }
                    if (!IsSerializedField(field))
                    {
                        continue;
                    }
                    FieldSchema? fieldSchema = AnalyzeField(field, ownerName, scriptTypeId);
                    if (fieldSchema != null)
                    {
                        schema.Fields.Add(fieldSchema);
                    }
                }
            }
            return schema;
        }

        // public field または [SerializeField] 付き field。static/const/readonly は対象外
        private static bool IsSerializedField(IFieldSymbol field)
        {
            if (field.IsStatic || field.IsConst || field.IsReadOnly)
            {
                return false;
            }
            bool hasSerializeField = field.GetAttributes()
                .Any(a => a.AttributeClass?.ToDisplayString() == SerializeFieldAttributeName);
            return field.DeclaredAccessibility == Accessibility.Public || hasSerializeField;
        }

        private static FieldSchema? AnalyzeField(IFieldSymbol field, string declaringType, string scriptTypeId)
        {
            var schema = new FieldSchema
            {
                Name = field.Name,
                DeclaringType = declaringType,
                IsPublic = field.DeclaredAccessibility == Accessibility.Public,
                Location = field.Locations.FirstOrDefault() ?? Location.None,
            };

            foreach (AttributeData attr in field.GetAttributes())
            {
                switch (attr.AttributeClass?.ToDisplayString())
                {
                    case SerializedFieldIdAttributeName:
                        if (attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string idValue)
                        {
                            schema.RawId = idValue;
                        }
                        break;
                    case FormerlySerializedAsAttributeName:
                        if (attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string oldName &&
                            !string.IsNullOrWhiteSpace(oldName))
                        {
                            schema.FormerNames.Add(oldName);
                        }
                        break;
                    case HideInInspectorAttributeName:
                        schema.IsHidden = true;
                        break;
                    case ReadOnlyAttributeName:
                        schema.IsReadOnly = true;
                        break;
                    case MultilineAttributeName:
                        schema.Multiline = true;
                        break;
                    case RangeAttributeName:
                        if (attr.ConstructorArguments.Length == 2)
                        {
                            schema.HasRange = true;
                            schema.RangeMin = ToFloat(attr.ConstructorArguments[0].Value);
                            schema.RangeMax = ToFloat(attr.ConstructorArguments[1].Value);
                        }
                        break;
                    case MinAttributeName:
                        if (attr.ConstructorArguments.Length == 1)
                        {
                            schema.HasMin = true;
                            schema.MinValue = ToFloat(attr.ConstructorArguments[0].Value);
                        }
                        break;
                    case DragSpeedAttributeName:
                        if (attr.ConstructorArguments.Length == 1)
                        {
                            schema.HasDragSpeed = true;
                            schema.DragSpeed = ToFloat(attr.ConstructorArguments[0].Value);
                        }
                        break;
                    case SeparatorTextAttributeName:
                        if (attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string separatorText)
                        {
                            schema.Header = separatorText;
                        }
                        break;
                    case LabelAttributeName:
                        if (attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string labelText)
                        {
                            schema.Label = labelText;
                        }
                        break;
                    case TooltipAttributeName:
                        if (attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string tipText)
                        {
                            schema.Tooltip = tipText;
                        }
                        break;
                }
            }

            schema.Kind = ResolveKind(field.Type);

            // origin name は rename を跨いで安定させるため、最も古い FormerlySerializedAs を優先する
            string originName = schema.FormerNames.Count > 0 ? schema.FormerNames[0] : schema.Name;
            if (!string.IsNullOrWhiteSpace(schema.RawId))
            {
                if (TryNormalizeGuid(schema.RawId, out string normalized))
                {
                    schema.FieldId = normalized;
                }
                else
                {
                    schema.RawIdInvalid = true;
                    schema.FieldId = DeterministicGuid("NEMEngine.ScriptField:" + scriptTypeId + "/" + declaringType + "/" + originName);
                }
            }
            else
            {
                schema.FieldId = DeterministicGuid("NEMEngine.ScriptField:" + scriptTypeId + "/" + declaringType + "/" + originName);
            }
            return schema;
        }

        // ITypeSymbol を valueKind へ分類する（collection / nullable は再帰）
        private static KindInfo ResolveKind(ITypeSymbol type)
        {
            // 配列
            if (type is IArrayTypeSymbol arrayType)
            {
                return new KindInfo { Kind = "Array", Element = ResolveKind(arrayType.ElementType) };
            }

            if (type is INamedTypeSymbol named)
            {
                // Nullable<T>
                if (named.IsGenericType && named.ConstructedFrom.SpecialType == SpecialType.System_Nullable_T)
                {
                    return new KindInfo { Kind = "Nullable", Element = ResolveKind(named.TypeArguments[0]) };
                }

                string constructed = named.ConstructedFrom.ToDisplayString();
                if (constructed == "System.Collections.Generic.List<T>")
                {
                    return new KindInfo { Kind = "List", Element = ResolveKind(named.TypeArguments[0]) };
                }

                // 参照型は型宣言の基底クラスだけで判定する（ScriptBehaviourはComponent派生なので先に判定）
                if (named.TypeKind == TypeKind.Class)
                {
                    if (DerivesFrom(named, AssetFullName))
                    {
                        return new KindInfo { Kind = "AssetRef", AssetType = ResolveAssetType(named) };
                    }
                    if (DerivesFrom(named, ScriptBehaviourFullName))
                    {
                        return new KindInfo
                        {
                            Kind = "ScriptRef",
                            ScriptType = named.ToDisplayString(SymbolDisplayFormat.FullyQualifiedFormat
                                .WithGlobalNamespaceStyle(SymbolDisplayGlobalNamespaceStyle.Omitted)),
                        };
                    }
                    if (!named.IsAbstract && DerivesFrom(named, ComponentFullName))
                    {
                        // 単純名がネイティブのコンポーネント登録名と一致する
                        return new KindInfo { Kind = "ComponentRef", ComponentType = named.Name };
                    }
                }
            }

            // enum
            if (type.TypeKind == TypeKind.Enum && type is INamedTypeSymbol enumType)
            {
                var info = new KindInfo
                {
                    Kind = "Enum",
                    EnumUnderlying = enumType.EnumUnderlyingType?.SpecialType.ToString() ?? "System_Int32",
                };
                foreach (ISymbol member in enumType.GetMembers())
                {
                    if (member is IFieldSymbol ef && ef.IsConst && ef.HasConstantValue)
                    {
                        info.EnumNames.Add(ef.Name);
                        info.EnumValues.Add(Convert.ToString(ef.ConstantValue, System.Globalization.CultureInfo.InvariantCulture) ?? "0");
                    }
                }
                return info;
            }

            // 既知の scalar / math / reference
            string fullName = type.ToDisplayString();
            string? scalar = ResolveScalarKind(fullName, type.SpecialType);
            if (scalar != null)
            {
                return new KindInfo { Kind = scalar };
            }
            return new KindInfo { Kind = "Unsupported" };
        }

        private static string? ResolveScalarKind(string fullName, SpecialType special)
        {
            switch (special)
            {
                case SpecialType.System_Boolean: return "Bool";
                case SpecialType.System_Byte: return "Byte";
                case SpecialType.System_SByte: return "SByte";
                case SpecialType.System_Int16: return "Short";
                case SpecialType.System_UInt16: return "UShort";
                case SpecialType.System_Int32: return "Int";
                case SpecialType.System_UInt32: return "UInt";
                case SpecialType.System_Int64: return "Long";
                case SpecialType.System_UInt64: return "ULong";
                case SpecialType.System_Single: return "Float";
                case SpecialType.System_Double: return "Double";
                case SpecialType.System_String: return "String";
            }
            switch (fullName)
            {
                case "NEMEngine.Vector2": return "Vector2";
                case "NEMEngine.Vector3": return "Vector3";
                case "NEMEngine.Vector4": return "Vector4";
                case "NEMEngine.Quaternion": return "Quaternion";
                case "NEMEngine.Color3": return "Color3";
                case "NEMEngine.Color4": return "Color4";
                case "NEMEngine.Entity": return "EntityRef";
            }
            return null;
        }

        // 任意の基底クラス名まで継承チェーンを辿る
        private static bool DerivesFrom(INamedTypeSymbol symbol, string baseFullName)
        {
            for (INamedTypeSymbol? current = symbol.BaseType; current != null; current = current.BaseType)
            {
                if (current.ToDisplayString() == baseFullName)
                {
                    return true;
                }
            }
            return false;
        }

        private static string ResolveAssetType(ITypeSymbol assetMarker)
        {
            foreach (AttributeData attr in assetMarker.GetAttributes())
            {
                if (attr.AttributeClass?.ToDisplayString() == NativeAssetTypeAttributeName &&
                    attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string nativeType)
                {
                    return nativeType;
                }
            }
            return "Unknown";
        }

        private static void Emit(SourceProductionContext spc, ImmutableArray<TypeSchema?> items,
            ImmutableArray<string> metaContents, bool validateOnly)
        {
            var schemas = items.Where(s => s != null).Select(s => s!).ToList();
            ScriptMetaIndex meta = ScriptMetaIndex.Build(metaContents);

            // ID 解決の優先順: 明示属性 → sidecar metadata → 決定的 fallback。formerNames も meta とマージ
            foreach (TypeSchema type in schemas)
            {
                if (!type.HasExplicitScriptId && meta.TryGetScriptId(type.FullTypeName, out string metaScriptId) &&
                    TryNormalizeGuid(metaScriptId, out string normalizedScriptId))
                {
                    type.ScriptTypeId = normalizedScriptId;
                }
                foreach (FieldSchema field in type.Fields)
                {
                    bool explicitField = !string.IsNullOrWhiteSpace(field.RawId) && !field.RawIdInvalid;
                    if (!explicitField && meta.TryGetFieldId(type.FullTypeName, field.Name, out string metaFieldId) &&
                        TryNormalizeGuid(metaFieldId, out string normalizedFieldId))
                    {
                        field.FieldId = normalizedFieldId;
                    }
                    foreach (string former in meta.GetFieldFormerNames(type.FullTypeName, field.Name))
                    {
                        if (former != field.Name && !field.FormerNames.Contains(former)) { field.FormerNames.Add(former); }
                    }
                }
            }

            foreach (TypeSchema type in schemas)
            {
                var seen = new Dictionary<string, string>(StringComparer.Ordinal);
                foreach (FieldSchema field in type.Fields)
                {
                    bool explicitField = !string.IsNullOrWhiteSpace(field.RawId) && !field.RawIdInvalid;
                    bool fromMeta = meta.TryGetFieldId(type.FullTypeName, field.Name, out _);
                    if (field.RawIdInvalid)
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(InvalidFieldIdRule, field.Location, type.FullTypeName, field.Name, field.RawId));
                    }
                    else if (!explicitField && !fromMeta)
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(
                            validateOnly ? MissingFieldIdValidateRule : MissingFieldIdRule, field.Location, type.FullTypeName, field.Name));
                    }
                    if (field.Kind.Kind == "Unsupported")
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(UnsupportedFieldRule, field.Location, type.FullTypeName, field.Name, "unsupported"));
                    }
                    if (seen.TryGetValue(field.FieldId, out string? otherName))
                    {
                        spc.ReportDiagnostic(Diagnostic.Create(DuplicateFieldIdRule, field.Location, type.FullTypeName, field.FieldId, otherName, field.Name));
                    }
                    else
                    {
                        seen[field.FieldId] = field.Name;
                    }
                }
            }

            // 決定的な JSON を組み立てる（script は GUID、field も GUID で安定ソート）
            var json = new StringBuilder();
            json.Append('{');
            json.Append("\"schemaVersion\":").Append(SchemaVersion).Append(',');
            json.Append("\"scripts\":[");
            bool firstType = true;
            foreach (TypeSchema type in schemas.OrderBy(s => s.ScriptTypeId, StringComparer.Ordinal))
            {
                if (!firstType) json.Append(',');
                firstType = false;
                json.Append('{');
                json.Append("\"scriptTypeId\":").Append(JsonString(type.ScriptTypeId)).Append(',');
                json.Append("\"fullTypeName\":").Append(JsonString(type.FullTypeName)).Append(',');
                json.Append("\"fields\":[");
                bool firstField = true;
                // Inspector 表示はソース宣言順にする（base→derived、宣言順）。
                // 宣言順は syntax 由来で決定的なので artifact の再現性も保たれる（reflection 列挙順には依存しない）。
                foreach (FieldSchema field in type.Fields)
                {
                    if (!firstField) json.Append(',');
                    firstField = false;
                    EmitField(json, field);
                }
                json.Append("]}");
            }
            json.Append("]}");

            var builder = new StringBuilder();
            builder.AppendLine("// <auto-generated/>");
            builder.AppendLine("#nullable enable");
            builder.AppendLine("namespace NEMEngine");
            builder.AppendLine("{");
            builder.AppendLine("    internal static class GeneratedScriptSchema");
            builder.AppendLine("    {");
            builder.Append("        public const string Json = ");
            builder.Append(VerbatimLiteral(json.ToString()));
            builder.AppendLine(";");
            builder.AppendLine("        public static string GetSchemaJson() { return Json; }");
            builder.AppendLine("    }");
            builder.AppendLine("}");

            spc.AddSource("GeneratedScriptSchema.g.cs", SourceText.From(builder.ToString(), Encoding.UTF8));
        }

        private static void EmitField(StringBuilder json, FieldSchema field)
        {
            json.Append('{');
            json.Append("\"fieldId\":").Append(JsonString(field.FieldId)).Append(',');
            json.Append("\"name\":").Append(JsonString(field.Name)).Append(',');
            json.Append("\"declaringType\":").Append(JsonString(field.DeclaringType)).Append(',');
            json.Append("\"isPublic\":").Append(field.IsPublic ? "true" : "false").Append(',');
            json.Append("\"isReadOnly\":").Append(field.IsReadOnly ? "true" : "false").Append(',');
            json.Append("\"isHidden\":").Append(field.IsHidden ? "true" : "false").Append(',');
            json.Append("\"multiline\":").Append(field.Multiline ? "true" : "false");
            if (field.FormerNames.Count > 0)
            {
                json.Append(",\"formerNames\":[");
                for (int i = 0; i < field.FormerNames.Count; ++i)
                {
                    if (i > 0) json.Append(',');
                    json.Append(JsonString(field.FormerNames[i]));
                }
                json.Append(']');
            }
            if (field.HasRange)
            {
                json.Append(",\"range\":{\"min\":").Append(FloatLiteral(field.RangeMin))
                    .Append(",\"max\":").Append(FloatLiteral(field.RangeMax)).Append('}');
            }
            if (field.HasMin)
            {
                json.Append(",\"min\":").Append(FloatLiteral(field.MinValue));
            }
            if (field.HasDragSpeed)
            {
                json.Append(",\"dragSpeed\":").Append(FloatLiteral(field.DragSpeed));
            }
            if (field.Tooltip != null)
            {
                json.Append(",\"tooltip\":").Append(JsonString(field.Tooltip));
            }
            if (field.Header != null)
            {
                json.Append(",\"header\":").Append(JsonString(field.Header));
            }
            if (field.Label != null)
            {
                json.Append(",\"label\":").Append(JsonString(field.Label));
            }
            json.Append(',');
            EmitKind(json, field.Kind);
            json.Append('}');
        }

        // "kind":"...", 付随情報, "element":{...} を出力する
        private static void EmitKind(StringBuilder json, KindInfo kind)
        {
            json.Append("\"kind\":").Append(JsonString(kind.Kind));
            if (kind.EnumUnderlying != null)
            {
                json.Append(",\"enumUnderlying\":").Append(JsonString(kind.EnumUnderlying));
                json.Append(",\"enumNames\":[");
                for (int i = 0; i < kind.EnumNames.Count; ++i)
                {
                    if (i > 0) json.Append(',');
                    json.Append(JsonString(kind.EnumNames[i]));
                }
                json.Append("],\"enumValues\":[");
                for (int i = 0; i < kind.EnumValues.Count; ++i)
                {
                    if (i > 0) json.Append(',');
                    json.Append(JsonString(kind.EnumValues[i]));
                }
                json.Append(']');
            }
            if (kind.AssetType != null)
            {
                json.Append(",\"assetType\":").Append(JsonString(kind.AssetType));
            }
            if (kind.ScriptType != null)
            {
                json.Append(",\"scriptType\":").Append(JsonString(kind.ScriptType));
            }
            if (kind.ComponentType != null)
            {
                json.Append(",\"componentType\":").Append(JsonString(kind.ComponentType));
            }
            if (kind.Element != null)
            {
                json.Append(",\"element\":{");
                EmitKind(json, kind.Element);
                json.Append('}');
            }
        }

        private static bool DerivesFromScriptBehaviour(INamedTypeSymbol symbol)
        {
            for (INamedTypeSymbol? current = symbol.BaseType; current != null; current = current.BaseType)
            {
                if (current.ToDisplayString() == ScriptBehaviourFullName)
                {
                    return true;
                }
            }
            return false;
        }

        private static string ResolveScriptTypeId(INamedTypeSymbol symbol, string fullName, out bool hasExplicit)
        {
            foreach (AttributeData attr in symbol.GetAttributes())
            {
                if (attr.AttributeClass?.ToDisplayString() == ScriptTypeIdAttributeName &&
                    attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string idValue &&
                    TryNormalizeGuid(idValue, out string normalized))
                {
                    hasExplicit = true;
                    return normalized;
                }
            }
            hasExplicit = false;
            return DeterministicGuid("NEMEngine.ScriptType:" + fullName);
        }

        private static float ToFloat(object? value)
        {
            try { return Convert.ToSingle(value, System.Globalization.CultureInfo.InvariantCulture); }
            catch { return 0.0f; }
        }

        private static string FloatLiteral(float value)
        {
            return value.ToString("R", System.Globalization.CultureInfo.InvariantCulture);
        }

        private static string JsonString(string value)
        {
            var sb = new StringBuilder();
            sb.Append('"');
            foreach (char c in value)
            {
                switch (c)
                {
                    case '"': sb.Append("\\\""); break;
                    case '\\': sb.Append("\\\\"); break;
                    case '\n': sb.Append("\\n"); break;
                    case '\r': sb.Append("\\r"); break;
                    case '\t': sb.Append("\\t"); break;
                    default:
                        if (c < 0x20) { sb.Append("\\u").Append(((int)c).ToString("x4")); }
                        else { sb.Append(c); }
                        break;
                }
            }
            sb.Append('"');
            return sb.ToString();
        }

        // C# verbatim string literal（@"..."、" を "" にエスケープ）として埋め込む
        private static string VerbatimLiteral(string value)
        {
            return "@\"" + value.Replace("\"", "\"\"") + "\"";
        }

        private static bool TryNormalizeGuid(string raw, out string normalized)
        {
            if (Guid.TryParse(raw, out Guid guid))
            {
                normalized = guid.ToString("D");
                return true;
            }
            normalized = string.Empty;
            return false;
        }

        private static string DeterministicGuid(string seed)
        {
            using var md5 = MD5.Create();
            byte[] hash = md5.ComputeHash(Encoding.UTF8.GetBytes(seed));
            return new Guid(hash).ToString("D");
        }
    }
}
