using System.Text;
using System.Globalization;

namespace NEMEngine;

// HLSL名またはShader Graphの公開UUIDから得る安定パラメータID
public readonly struct MaterialParameterID : IEquatable<MaterialParameterID> {

    public readonly ulong value;

    public MaterialParameterID(ulong value) {
        this.value = value;
    }

    public bool isValid => value != 0ul;

    // C++側と同じUTF-8 FNV-1aで既存HLSL変数名をID化する
    public static MaterialParameterID FromName(string name) {

        ArgumentException.ThrowIfNullOrEmpty(name);
        int byteCount = Encoding.UTF8.GetByteCount(name);
        Span<byte> bytes = byteCount <= 256
            ? stackalloc byte[byteCount]
            : new byte[byteCount];
        Encoding.UTF8.GetBytes(name, bytes);

        ulong hash = 14695981039346656037ul;
        foreach (byte value in bytes) {
            hash ^= value;
            hash *= 1099511628211ul;
        }
        return new MaterialParameterID(hash != 0ul ? hash : 1ul);
    }

    // Shader Graphエディターに表示される16桁IDから生成する
    public static MaterialParameterID FromHex(string value) {

        ArgumentException.ThrowIfNullOrEmpty(value);
        if (value.Length != 16 ||
            !ulong.TryParse(
            value, NumberStyles.HexNumber,
            CultureInfo.InvariantCulture,
            out ulong parsed) ||
            parsed == 0ul) {

            throw new FormatException(
                "Material parameter ID must be a non-zero 16-digit hexadecimal value.");
        }
        return new MaterialParameterID(parsed);
    }

    public bool Equals(MaterialParameterID other) => value == other.value;
    public override bool Equals(object? obj) =>
        obj is MaterialParameterID other && Equals(other);
    public override int GetHashCode() => value.GetHashCode();
    public static bool operator ==(MaterialParameterID left, MaterialParameterID right) =>
        left.Equals(right);
    public static bool operator !=(MaterialParameterID left, MaterialParameterID right) =>
        !left.Equals(right);
}

// 標準PBRシェーダーと共有する公開パラメータ名
public static class MaterialParameterNames {

    public const string BaseColor = "color";
    public const string BaseColorTexture = "baseColorTexture";
    public const string NormalTexture = "normalTexture";
    public const string Metallic = "Metallic";
    public const string MetallicRoughnessTexture = "metallicRoughnessTexture";
    public const string Roughness = "Roughness";
    public const string AmbientOcclusion = "ambientOcclusion";
    public const string AmbientOcclusionTexture = "occlusionTexture";
    public const string EmissiveColor = "emissiveColor";
    public const string EmissiveTexture = "emissiveTexture";
    public const string EmissiveIntensity = "emissiveIntensity";
    public const string Opacity = "opacity";
    public const string AlphaClip = "alphaClip";
}

// 標準PBRパラメータの安定ID
public static class MaterialParameterIDs {

    public static readonly MaterialParameterID BaseColor =
        MaterialParameterID.FromName(MaterialParameterNames.BaseColor);
    public static readonly MaterialParameterID BaseColorTexture =
        MaterialParameterID.FromName(MaterialParameterNames.BaseColorTexture);
    public static readonly MaterialParameterID NormalTexture =
        MaterialParameterID.FromName(MaterialParameterNames.NormalTexture);
    public static readonly MaterialParameterID Metallic =
        MaterialParameterID.FromName(MaterialParameterNames.Metallic);
    public static readonly MaterialParameterID MetallicRoughnessTexture =
        MaterialParameterID.FromName(MaterialParameterNames.MetallicRoughnessTexture);
    public static readonly MaterialParameterID Roughness =
        MaterialParameterID.FromName(MaterialParameterNames.Roughness);
    public static readonly MaterialParameterID AmbientOcclusion =
        MaterialParameterID.FromName(MaterialParameterNames.AmbientOcclusion);
    public static readonly MaterialParameterID AmbientOcclusionTexture =
        MaterialParameterID.FromName(MaterialParameterNames.AmbientOcclusionTexture);
    public static readonly MaterialParameterID EmissiveColor =
        MaterialParameterID.FromName(MaterialParameterNames.EmissiveColor);
    public static readonly MaterialParameterID EmissiveTexture =
        MaterialParameterID.FromName(MaterialParameterNames.EmissiveTexture);
    public static readonly MaterialParameterID EmissiveIntensity =
        MaterialParameterID.FromName(MaterialParameterNames.EmissiveIntensity);
    public static readonly MaterialParameterID Opacity =
        MaterialParameterID.FromName(MaterialParameterNames.Opacity);
    public static readonly MaterialParameterID AlphaClip =
        MaterialParameterID.FromName(MaterialParameterNames.AlphaClip);
}

// Renderer上のMaterial Assetへ重ねるEntity固有パラメータ
public readonly struct MaterialInstance {

    private readonly Entity entity;
    private readonly RendererMaterialTarget target;
    private readonly int subMeshIndex;

    internal MaterialInstance(
        Entity entity, RendererMaterialTarget target, int subMeshIndex = -1) {

        this.entity = entity;
        this.target = target;
        this.subMeshIndex = subMeshIndex;
    }

    public bool SetFloat(string name, float value) =>
        SetFloat(MaterialParameterID.FromName(name), name, value);

    public bool SetFloat(MaterialParameterID id, string name, float value) {
        NativeMaterialParameterValue native = new() {
            x = value,
            type = NativeMaterialParameterValueType.Float
        };
        return Set(id, name, native);
    }

    public bool SetVector(string name, Vector2 value) =>
        SetVector(MaterialParameterID.FromName(name), name, value);

    public bool SetVector(MaterialParameterID id, string name, Vector2 value) {
        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            type = NativeMaterialParameterValueType.Vector2
        };
        return Set(id, name, native);
    }

    public bool SetVector(string name, Vector3 value) =>
        SetVector(MaterialParameterID.FromName(name), name, value);

    public bool SetVector(MaterialParameterID id, string name, Vector3 value) {
        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            z = value.z,
            type = NativeMaterialParameterValueType.Vector3
        };
        return Set(id, name, native);
    }

    public bool SetVector(string name, Vector4 value) =>
        SetVector(MaterialParameterID.FromName(name), name, value);

    public bool SetVector(MaterialParameterID id, string name, Vector4 value) {
        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w,
            type = NativeMaterialParameterValueType.Vector4
        };
        return Set(id, name, native);
    }

    public bool SetColor(string name, Color4 value) =>
        SetColor(MaterialParameterID.FromName(name), name, value);

    public bool SetColor(MaterialParameterID id, string name, Color4 value) {
        NativeMaterialParameterValue native = new() {
            x = value.r,
            y = value.g,
            z = value.b,
            w = value.a,
            type = NativeMaterialParameterValueType.Color
        };
        return Set(id, name, native);
    }

    public bool SetTexture(string name, Texture? value) =>
        SetTexture(MaterialParameterID.FromName(name), name, value);

    public bool SetTexture(MaterialParameterID id, string name, Texture? value) {
        NativeMaterialParameterValue native = new() {
            assetID = value?.assetID ?? AssetGUID.None,
            type = NativeMaterialParameterValueType.Texture
        };
        return Set(id, name, native);
    }

    public bool SetInt(string name, int value) =>
        SetInt(MaterialParameterID.FromName(name), name, value);

    public bool SetInt(MaterialParameterID id, string name, int value) {
        NativeMaterialParameterValue native = new() {
            intValue = value,
            type = NativeMaterialParameterValueType.Int
        };
        return Set(id, name, native);
    }

    public bool SetUInt(string name, uint value) =>
        SetUInt(MaterialParameterID.FromName(name), name, value);

    public bool SetUInt(MaterialParameterID id, string name, uint value) {
        NativeMaterialParameterValue native = new() {
            uintValue = value,
            type = NativeMaterialParameterValueType.UInt
        };
        return Set(id, name, native);
    }

    public bool SetBool(string name, bool value) =>
        SetBool(MaterialParameterID.FromName(name), name, value);

    public bool SetBool(MaterialParameterID id, string name, bool value) {
        NativeMaterialParameterValue native = new() {
            intValue = value ? 1 : 0,
            type = NativeMaterialParameterValueType.Bool
        };
        return Set(id, name, native);
    }

    public bool TryGetFloat(string name, out float value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Float ? native.x : 0.0f;
        return found && native.type == NativeMaterialParameterValueType.Float;
    }

    public bool TryGetVector2(string name, out Vector2 value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Vector2
            ? new Vector2(native.x, native.y)
            : Vector2.zero;
        return found && native.type == NativeMaterialParameterValueType.Vector2;
    }

    public bool TryGetVector3(string name, out Vector3 value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Vector3
            ? new Vector3(native.x, native.y, native.z)
            : Vector3.zero;
        return found && native.type == NativeMaterialParameterValueType.Vector3;
    }

    public bool TryGetVector4(string name, out Vector4 value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Vector4
            ? new Vector4(native.x, native.y, native.z, native.w)
            : Vector4.zero;
        return found && native.type == NativeMaterialParameterValueType.Vector4;
    }

    public bool TryGetColor(string name, out Color4 value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        bool compatible = found &&
            (native.type == NativeMaterialParameterValueType.Color ||
                native.type == NativeMaterialParameterValueType.Vector4);
        value = compatible
            ? new Color4(native.x, native.y, native.z, native.w)
            : new Color4(1.0f, 1.0f, 1.0f, 1.0f);
        return compatible;
    }

    public bool TryGetTexture(string name, out Texture? value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        bool compatible = found && native.type == NativeMaterialParameterValueType.Texture;
        value = compatible && native.assetID.isValid
            ? new Texture(native.assetID)
            : null;
        return compatible;
    }

    public bool TryGetInt(string name, out int value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Int ? native.intValue : 0;
        return found && native.type == NativeMaterialParameterValueType.Int;
    }

    public bool TryGetUInt(string name, out uint value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.UInt ? native.uintValue : 0u;
        return found && native.type == NativeMaterialParameterValueType.UInt;
    }

    public bool TryGetBool(string name, out bool value) {
        bool found = TryGet(MaterialParameterID.FromName(name), out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Bool &&
            native.intValue != 0;
        return found && native.type == NativeMaterialParameterValueType.Bool;
    }

    public bool Clear(string name) =>
        Clear(MaterialParameterID.FromName(name));

    public bool Clear(MaterialParameterID id) {
        return NativeAPI.ClearRendererMaterialParameterValue(
            entity.native, target, subMeshIndex, id.value);
    }

    private bool Set(
        MaterialParameterID id, string name,
        NativeMaterialParameterValue value) {

        ArgumentException.ThrowIfNullOrEmpty(name);
        return NativeAPI.WriteRendererMaterialParameter(
            entity.native, target, subMeshIndex, id.value, name, value);
    }

    private bool TryGet(
        MaterialParameterID id,
        out NativeMaterialParameterValue value) {

        return NativeAPI.ReadRendererMaterialParameter(
            entity.native, target, subMeshIndex, id.value, out value);
    }
}
