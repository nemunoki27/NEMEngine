using System.Text;
using System.Text.Json;
using static NEM.ComponentBindingGen.BindingTypeLayout;
using static NEM.ComponentBindingGen.BindingOutputText;
using static NEM.ComponentBindingGen.NativeBindingEmitter;
using static NEM.ComponentBindingGen.ManagedBindingEmitter;
using static NEM.ComponentBindingGen.BindingArtifactStore;

namespace NEM.ComponentBindingGen;

// Component連携の生成処理
internal static class BindingTypeLayout {

    internal static bool IsKnownKind(string kind) {
        switch (kind) {
            case "Bool": case "Byte": case "SByte": case "Short": case "UShort":
            case "Int": case "UInt": case "Long": case "ULong": case "Float": case "Double":
            case "String": case "Enum":
            case "Vector2": case "Vector3": case "Vector4": case "Quaternion":
            case "Color3": case "Color4": case "AssetRef": case "EntityRef":
                return true;
            default: return false;
        }
    }

    internal static (string csType, int size) PodInfo(string kind) {
        switch (kind) {
            case "Byte": return ("byte", 1);
            case "SByte": return ("sbyte", 1);
            case "Short": return ("short", 2);
            case "UShort": return ("ushort", 2);
            case "Int": return ("int", 4);
            case "UInt": return ("uint", 4);
            case "Long": return ("long", 8);
            case "ULong": return ("ulong", 8);
            case "Float": return ("float", 4);
            case "Double": return ("double", 8);
            case "Vector2": return ("Vector2", 8);
            case "Vector3": return ("Vector3", 12);
            case "Vector4": return ("Vector4", 16);
            case "Quaternion": return ("Quaternion", 16);
            case "Color3": return ("Color3", 12);
            case "Color4": return ("Color4", 16);
            case "EntityRef": return ("GameObject", 16);
            default: return ("", 0);
        }
    }
}
