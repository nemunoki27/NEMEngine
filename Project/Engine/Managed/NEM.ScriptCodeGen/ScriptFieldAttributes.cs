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
using static NEM.ScriptCodeGen.ScriptKindResolver;
using static NEM.ScriptCodeGen.ScriptIdentity;
using static NEM.ScriptCodeGen.ScriptSchemaText;

namespace NEM.ScriptCodeGen
{
    // スクリプト生成の属性解析
    internal static class ScriptFieldAttributes
    {
        internal static void ParseFieldAttributes(IFieldSymbol field, FieldSchema schema)
        {
            foreach (AttributeData attr in field.GetAttributes())
            {
                switch (attr.AttributeClass?.ToDisplayString())
                {
                    case SerializedFieldIDAttributeName:
                        if (attr.ConstructorArguments.Length == 1 && attr.ConstructorArguments[0].Value is string idValue)
                        {
                            schema.RawID = idValue;
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

        }
    }
}
