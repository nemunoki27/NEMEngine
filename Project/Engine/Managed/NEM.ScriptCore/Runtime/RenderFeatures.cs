namespace NEMEngine;

//============================================================================
//	RenderFeatures class
//	現在のRenderFeatureProfileへ実行時オーバーライドを設定する
//============================================================================
public static class RenderFeatures {

    // GPUがDXRをサポートしている場合にtrueを返す
    public static bool IsRayTracingSupported => NativeApi.ReadRayTracingSupported();

    // グラフィック設定を含めDispatchRaysが現在使用可能な場合にtrueを返す
    public static bool IsRayTracingActive => NativeApi.ReadRayTracingActive();

	// Profile内の表示名と一致するパスの有効状態を変更する
	public static bool SetEnabled(string passName, bool enabled) =>
		NativeApi.WriteRenderFeaturePassEnabled(passName, enabled);

    public static bool SetFloat(string passName, string name, float value) =>
        SetFloat(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetFloat(
        string passName, MaterialParameterID id, string name, float value) {

        NativeMaterialParameterValue native = new() {
            x = value,
            type = NativeMaterialParameterValueType.Float
        };
        return Set(passName, id, name, native);
    }

    public static bool SetVector(string passName, string name, Vector2 value) =>
        SetVector(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetVector(
        string passName, MaterialParameterID id, string name, Vector2 value) {

        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            type = NativeMaterialParameterValueType.Vector2
        };
        return Set(passName, id, name, native);
    }

    public static bool SetVector(string passName, string name, Vector3 value) =>
        SetVector(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetVector(
        string passName, MaterialParameterID id, string name, Vector3 value) {

        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            z = value.z,
            type = NativeMaterialParameterValueType.Vector3
        };
        return Set(passName, id, name, native);
    }

    public static bool SetVector(string passName, string name, Vector4 value) =>
        SetVector(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetVector(
        string passName, MaterialParameterID id, string name, Vector4 value) {

        NativeMaterialParameterValue native = new() {
            x = value.x,
            y = value.y,
            z = value.z,
            w = value.w,
            type = NativeMaterialParameterValueType.Vector4
        };
        return Set(passName, id, name, native);
    }

    public static bool SetColor(string passName, string name, Color4 value) =>
        SetColor(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetColor(
        string passName, MaterialParameterID id, string name, Color4 value) {

        NativeMaterialParameterValue native = new() {
            x = value.r,
            y = value.g,
            z = value.b,
            w = value.a,
            type = NativeMaterialParameterValueType.Color
        };
        return Set(passName, id, name, native);
    }

    public static bool SetTexture(string passName, string name, Texture? value) =>
        SetTexture(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetTexture(
        string passName, MaterialParameterID id, string name, Texture? value) {

        NativeMaterialParameterValue native = new() {
            assetID = value?.assetId ?? AssetGUID.None,
            type = NativeMaterialParameterValueType.Texture
        };
        return Set(passName, id, name, native);
    }

    public static bool SetInt(string passName, string name, int value) =>
        SetInt(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetInt(
        string passName, MaterialParameterID id, string name, int value) {

        NativeMaterialParameterValue native = new() {
            intValue = value,
            type = NativeMaterialParameterValueType.Int
        };
        return Set(passName, id, name, native);
    }

    public static bool SetUInt(string passName, string name, uint value) =>
        SetUInt(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetUInt(
        string passName, MaterialParameterID id, string name, uint value) {

        NativeMaterialParameterValue native = new() {
            uintValue = value,
            type = NativeMaterialParameterValueType.UInt
        };
        return Set(passName, id, name, native);
    }

    public static bool SetBool(string passName, string name, bool value) =>
        SetBool(passName, MaterialParameterID.FromName(name), name, value);

    public static bool SetBool(
        string passName, MaterialParameterID id, string name, bool value) {

        NativeMaterialParameterValue native = new() {
            intValue = value ? 1 : 0,
            type = NativeMaterialParameterValueType.Bool
        };
        return Set(passName, id, name, native);
    }

    // パラメータ1件をProfileの保存値へ戻す
    public static bool ClearParameter(string passName, string name) =>
        ClearParameter(passName, MaterialParameterID.FromName(name));

	public static bool ClearParameter(
		string passName, MaterialParameterID id) =>
		NativeApi.ClearRenderFeaturePassParameterValue(passName, id.value);

	// パスの有効状態と全パラメータをProfileの保存値へ戻す
	public static bool ResetPass(string passName) =>
		NativeApi.ResetRenderFeaturePassValue(passName);

	// 全パスの実行時変更をProfileの保存値へ戻す
	public static void ResetAll() =>
		NativeApi.ResetAllRenderFeatureOverrides();

    // 型付き値をNative APIへ渡す
    private static bool Set(
        string passName, MaterialParameterID id, string name,
        NativeMaterialParameterValue value) {

		return NativeApi.WriteRenderFeaturePassParameter(
			passName, id.value, name, value);
    }
}
