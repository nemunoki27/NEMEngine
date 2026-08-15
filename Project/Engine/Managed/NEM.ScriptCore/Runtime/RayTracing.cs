namespace NEMEngine;

//============================================================================
//	RayTracing class
//	現在のRayTracingProfileへ実行時オーバーライドを設定する
//============================================================================
public static class RayTracing {

    // GPUがDXRをサポートしている場合にtrueを返す
    public static bool IsSupported => NativeApi.ReadRayTracingSupported();

    // グラフィック設定を含めDispatchRaysが現在使用可能な場合にtrueを返す
    public static bool IsActive => NativeApi.ReadRayTracingActive();

    // Profile内の表示名と一致するエフェクトの有効状態を変更する
    public static bool SetEnabled(string effectName, bool enabled) =>
        NativeApi.WriteRayTracingEffectEnabled(effectName, enabled);

    public static bool SetFloat(string effectName, string name, float value) =>
        SetFloat(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetFloat(
        string effectName, MaterialParameterID id, string name, float value) {

        NativeMaterialParameterValue native = new() {
            x = value,
            type = NativeMaterialParameterValueType.Float
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetVector(string effectName, string name, Vector2 value) =>
        SetVector(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetVector(
        string effectName, MaterialParameterID id, string name, Vector2 value) {

        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            type = NativeMaterialParameterValueType.Vector2
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetVector(string effectName, string name, Vector3 value) =>
        SetVector(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetVector(
        string effectName, MaterialParameterID id, string name, Vector3 value) {

        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            z = value.z,
            type = NativeMaterialParameterValueType.Vector3
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetVector(string effectName, string name, Vector4 value) =>
        SetVector(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetVector(
        string effectName, MaterialParameterID id, string name, Vector4 value) {

        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w,
            type = NativeMaterialParameterValueType.Vector4
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetColor(string effectName, string name, Color4 value) =>
        SetColor(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetColor(
        string effectName, MaterialParameterID id, string name, Color4 value) {

        NativeMaterialParameterValue native = new() {
            x = value.r,
            y = value.g,
            z = value.b,
            w = value.a,
            type = NativeMaterialParameterValueType.Color
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetTexture(string effectName, string name, Texture? value) =>
        SetTexture(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetTexture(
        string effectName, MaterialParameterID id, string name, Texture? value) {

        NativeMaterialParameterValue native = new() {
            assetID = value?.assetId ?? AssetGUID.None,
            type = NativeMaterialParameterValueType.Texture
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetInt(string effectName, string name, int value) =>
        SetInt(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetInt(
        string effectName, MaterialParameterID id, string name, int value) {

        NativeMaterialParameterValue native = new() {
            intValue = value,
            type = NativeMaterialParameterValueType.Int
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetUInt(string effectName, string name, uint value) =>
        SetUInt(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetUInt(
        string effectName, MaterialParameterID id, string name, uint value) {

        NativeMaterialParameterValue native = new() {
            uintValue = value,
            type = NativeMaterialParameterValueType.UInt
        };
        return Set(effectName, id, name, native);
    }

    public static bool SetBool(string effectName, string name, bool value) =>
        SetBool(effectName, MaterialParameterID.FromName(name), name, value);

    public static bool SetBool(
        string effectName, MaterialParameterID id, string name, bool value) {

        NativeMaterialParameterValue native = new() {
            intValue = value ? 1 : 0,
            type = NativeMaterialParameterValueType.Bool
        };
        return Set(effectName, id, name, native);
    }

    // パラメータ1件をProfileの保存値へ戻す
    public static bool ClearParameter(string effectName, string name) =>
        ClearParameter(effectName, MaterialParameterID.FromName(name));

    public static bool ClearParameter(
        string effectName, MaterialParameterID id) =>
        NativeApi.ClearRayTracingEffectParameterValue(effectName, id.value);

    // エフェクトの有効状態と全パラメータをProfileの保存値へ戻す
    public static bool ResetEffect(string effectName) =>
        NativeApi.ResetRayTracingEffectValue(effectName);

    // 全エフェクトの実行時変更をProfileの保存値へ戻す
    public static void ResetAll() =>
        NativeApi.ResetAllRayTracingOverrides();

    // 型付き値をNative APIへ渡す
    private static bool Set(
        string effectName, MaterialParameterID id, string name,
        NativeMaterialParameterValue value) {

        return NativeApi.WriteRayTracingEffectParameter(
            effectName, id.value, name, value);
    }
}
