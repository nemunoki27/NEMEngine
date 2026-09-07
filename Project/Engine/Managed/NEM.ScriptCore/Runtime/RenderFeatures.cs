namespace NEMEngine;

//============================================================================
//	RenderFeaturePass struct
//	Profile内の1つの描画Passへ実行時オーバーライドを設定する
//============================================================================
public readonly struct RenderFeaturePass {

    private readonly ulong passID;
    private readonly ulong generation;

    internal RenderFeaturePass(ulong passID, ulong generation) {
        this.passID = passID;
        this.generation = generation;
    }

    // Profile切替後の古いハンドルを含めて現在操作可能な場合にtrueを返す
    public bool isValid => NativeApi.ValidateRenderFeaturePassValue(
        passID, generation);

    public bool SetEnabled(bool enabled) =>
        NativeApi.WriteRenderFeaturePassEnabled(
            passID, generation, enabled);

    // 同じ実行位置のSceneColor出力をこのパスへ切り替える
    public bool SetSceneColorOutput(bool enabled) =>
        NativeApi.WriteRenderFeaturePassSceneColorOutput(passID, generation, enabled);

    public bool SetFloat(string name, float value) =>
        SetFloat(MaterialParameterID.FromName(name), name, value);

    public bool SetFloat(
        MaterialParameterID id, string name, float value) =>
        Set(id, name, new NativeMaterialParameterValue {
            x = value,
            type = NativeMaterialParameterValueType.Float
        });

    public bool SetVector(string name, Vector2 value) =>
        SetVector(MaterialParameterID.FromName(name), name, value);

    public bool SetVector(
        MaterialParameterID id, string name, Vector2 value) =>
        Set(id, name, new NativeMaterialParameterValue {
            x = value.x,
            y = value.y,
            type = NativeMaterialParameterValueType.Vector2
        });

    public bool SetVector(string name, Vector3 value) =>
        SetVector(MaterialParameterID.FromName(name), name, value);

    public bool SetVector(
        MaterialParameterID id, string name, Vector3 value) =>
        Set(id, name, new NativeMaterialParameterValue {
            x = value.x,
            y = value.y,
            z = value.z,
            type = NativeMaterialParameterValueType.Vector3
        });

    public bool SetVector(string name, Vector4 value) =>
        SetVector(MaterialParameterID.FromName(name), name, value);

    public bool SetVector(
        MaterialParameterID id, string name, Vector4 value) =>
        Set(id, name, new NativeMaterialParameterValue {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w,
            type = NativeMaterialParameterValueType.Vector4
        });

    public bool SetColor(string name, Color4 value) =>
        SetColor(MaterialParameterID.FromName(name), name, value);

    public bool SetColor(
        MaterialParameterID id, string name, Color4 value) =>
        Set(id, name, new NativeMaterialParameterValue {
            x = value.r,
            y = value.g,
            z = value.b,
            w = value.a,
            type = NativeMaterialParameterValueType.Color
        });

    public bool SetTexture(string name, Texture? value) =>
        SetTexture(MaterialParameterID.FromName(name), name, value);

    public bool SetTexture(
        MaterialParameterID id, string name, Texture? value) =>
        Set(id, name, new NativeMaterialParameterValue {
            assetID = value?.assetId ?? AssetGUID.None,
            type = NativeMaterialParameterValueType.Texture
        });

    public bool SetInt(string name, int value) =>
        SetInt(MaterialParameterID.FromName(name), name, value);

    public bool SetInt(
        MaterialParameterID id, string name, int value) =>
        Set(id, name, new NativeMaterialParameterValue {
            intValue = value,
            type = NativeMaterialParameterValueType.Int
        });

    public bool SetUInt(string name, uint value) =>
        SetUInt(MaterialParameterID.FromName(name), name, value);

    public bool SetUInt(
        MaterialParameterID id, string name, uint value) =>
        Set(id, name, new NativeMaterialParameterValue {
            uintValue = value,
            type = NativeMaterialParameterValueType.UInt
        });

    public bool SetBool(string name, bool value) =>
        SetBool(MaterialParameterID.FromName(name), name, value);

    public bool SetBool(
        MaterialParameterID id, string name, bool value) =>
        Set(id, name, new NativeMaterialParameterValue {
            intValue = value ? 1 : 0,
            type = NativeMaterialParameterValueType.Bool
        });

    public bool TryGetFloat(string name, out float value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Float
            ? native.x : 0.0f;
        return found && native.type == NativeMaterialParameterValueType.Float;
    }

    public bool TryGetVector2(string name, out Vector2 value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Vector2
            ? new Vector2(native.x, native.y) : Vector2.zero;
        return found && native.type == NativeMaterialParameterValueType.Vector2;
    }

    public bool TryGetVector3(string name, out Vector3 value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Vector3
            ? new Vector3(native.x, native.y, native.z) : Vector3.zero;
        return found && native.type == NativeMaterialParameterValueType.Vector3;
    }

    public bool TryGetVector4(string name, out Vector4 value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Vector4
            ? new Vector4(native.x, native.y, native.z, native.w) : Vector4.zero;
        return found && native.type == NativeMaterialParameterValueType.Vector4;
    }

    public bool TryGetColor(string name, out Color4 value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        bool compatible = found &&
            (native.type == NativeMaterialParameterValueType.Color ||
                native.type == NativeMaterialParameterValueType.Vector4);
        value = compatible
            ? new Color4(native.x, native.y, native.z, native.w)
            : new Color4(1.0f, 1.0f, 1.0f, 1.0f);
        return compatible;
    }

    public bool TryGetTexture(string name, out Texture? value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        bool compatible = found &&
            native.type == NativeMaterialParameterValueType.Texture;
        value = compatible && native.assetID.isValid
            ? new Texture(native.assetID) : null;
        return compatible;
    }

    public bool TryGetInt(string name, out int value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Int
            ? native.intValue : 0;
        return found && native.type == NativeMaterialParameterValueType.Int;
    }

    public bool TryGetUInt(string name, out uint value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.UInt
            ? native.uintValue : 0u;
        return found && native.type == NativeMaterialParameterValueType.UInt;
    }

    public bool TryGetBool(string name, out bool value) {
        bool found = TryGet(MaterialParameterID.FromName(name),
            out NativeMaterialParameterValue native);
        value = found && native.type == NativeMaterialParameterValueType.Bool &&
            native.intValue != 0;
        return found && native.type == NativeMaterialParameterValueType.Bool;
    }

    public bool ClearParameter(string name) =>
        ClearParameter(MaterialParameterID.FromName(name));

    public bool ClearParameter(MaterialParameterID id) =>
        NativeApi.ClearRenderFeaturePassParameterValue(
            passID, generation, id.value);

    public bool Reset() =>
        NativeApi.ResetRenderFeaturePassValue(passID, generation);

    private bool Set(
        MaterialParameterID id, string name,
        NativeMaterialParameterValue value) {

        ArgumentException.ThrowIfNullOrEmpty(name);
        return NativeApi.WriteRenderFeaturePassParameter(
            passID, generation, id.value, name, value);
    }

    private bool TryGet(
        MaterialParameterID id,
        out NativeMaterialParameterValue value) =>
        NativeApi.ReadRenderFeaturePassParameter(
            passID, generation, id.value, out value);
}

//============================================================================
//	RenderFeatures class
//	現在のRenderFeatureProfileへ実行時オーバーライドを設定する
//============================================================================
public static class RenderFeatures {

    // GPUがDXRをサポートしている場合にtrueを返す
    public static bool IsRayTracingSupported =>
        NativeApi.ReadRayTracingSupported();

    // グラフィック設定を含めDispatchRaysが現在使用可能な場合にtrueを返す
    public static bool IsRayTracingActive =>
        NativeApi.ReadRayTracingActive();

    // Profile内の表示名からPassハンドルを取得する
    public static RenderFeaturePass FindPass(string passName) {
        ArgumentException.ThrowIfNullOrEmpty(passName);
        return NativeApi.ResolveRenderFeaturePassValue(
            passName, out ulong passID, out ulong generation)
            ? new RenderFeaturePass(passID, generation)
            : default;
    }

    public static bool SetSceneColorOutput(string passName, bool enabled) =>
        FindPass(passName).SetSceneColorOutput(enabled);

    public static bool SetEnabled(string passName, bool enabled) =>
        FindPass(passName).SetEnabled(enabled);

    // Profile内の表示名と一致するグループの有効状態を変更する
    public static bool SetGroupEnabled(string groupName, bool enabled) =>
        NativeApi.WriteRenderFeatureGroupEnabled(groupName, enabled);

    public static bool SetFloat(string passName, string name, float value) =>
        FindPass(passName).SetFloat(name, value);

    public static bool SetFloat(string passName,
        MaterialParameterID id, string name, float value) =>
        FindPass(passName).SetFloat(id, name, value);

    public static bool SetVector(string passName, string name, Vector2 value) =>
        FindPass(passName).SetVector(name, value);

    public static bool SetVector(string passName,
        MaterialParameterID id, string name, Vector2 value) =>
        FindPass(passName).SetVector(id, name, value);

    public static bool SetVector(string passName, string name, Vector3 value) =>
        FindPass(passName).SetVector(name, value);

    public static bool SetVector(string passName,
        MaterialParameterID id, string name, Vector3 value) =>
        FindPass(passName).SetVector(id, name, value);

    public static bool SetVector(string passName, string name, Vector4 value) =>
        FindPass(passName).SetVector(name, value);

    public static bool SetVector(string passName,
        MaterialParameterID id, string name, Vector4 value) =>
        FindPass(passName).SetVector(id, name, value);

    public static bool SetColor(string passName, string name, Color4 value) =>
        FindPass(passName).SetColor(name, value);

    public static bool SetColor(string passName,
        MaterialParameterID id, string name, Color4 value) =>
        FindPass(passName).SetColor(id, name, value);

    public static bool SetTexture(string passName, string name, Texture? value) =>
        FindPass(passName).SetTexture(name, value);

    public static bool SetTexture(string passName,
        MaterialParameterID id, string name, Texture? value) =>
        FindPass(passName).SetTexture(id, name, value);

    public static bool SetInt(string passName, string name, int value) =>
        FindPass(passName).SetInt(name, value);

    public static bool SetInt(string passName,
        MaterialParameterID id, string name, int value) =>
        FindPass(passName).SetInt(id, name, value);

    public static bool SetUInt(string passName, string name, uint value) =>
        FindPass(passName).SetUInt(name, value);

    public static bool SetUInt(string passName,
        MaterialParameterID id, string name, uint value) =>
        FindPass(passName).SetUInt(id, name, value);

    public static bool SetBool(string passName, string name, bool value) =>
        FindPass(passName).SetBool(name, value);

    public static bool SetBool(string passName,
        MaterialParameterID id, string name, bool value) =>
        FindPass(passName).SetBool(id, name, value);

    public static bool ClearParameter(string passName, string name) =>
        FindPass(passName).ClearParameter(name);

    public static bool ClearParameter(string passName,
        MaterialParameterID id) => FindPass(passName).ClearParameter(id);

    public static bool ResetPass(string passName) =>
        FindPass(passName).Reset();

    // 全Passの実行時変更をProfileの保存値へ戻す
    public static void ResetAll() =>
        NativeApi.ResetAllRenderFeatureOverrides();
}
