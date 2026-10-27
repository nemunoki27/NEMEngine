namespace NEMEngine;

//============================================================================
//	NativeAssetSettingsAPI class
//	共有Assetの実行中変更をNativeへ渡す
//============================================================================
internal static unsafe class NativeAssetSettingsAPI {

	internal static bool Read(AssetGUID asset, ulong id, out NativeMaterialParameterValue value) {
		NativeMaterialParameterValue result = default;
		bool found = NativeAPI.GetAssetRuntimeParameter != null && NativeAPI.GetAssetRuntimeParameter(asset, id, &result) != 0;
		value = result;
		return found;
	}
	internal static bool Write(AssetGUID asset, ulong id, string name, NativeMaterialParameterValue value) {
		if (NativeAPI.SetAssetRuntimeParameter == null) return false;
		byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name);
		fixed (byte* data = bytes) return NativeAPI.SetAssetRuntimeParameter(asset, id, data, &value) != 0;
	}
	internal static bool Clear(AssetGUID asset, ulong id) => NativeAPI.ClearAssetRuntimeParameter != null &&
		NativeAPI.ClearAssetRuntimeParameter(asset, id) != 0;
}

public sealed partial class Material {

	// 同じAssetを参照する全Rendererへ反映する
	public MaterialInstance parameters => new(this);
}

public sealed partial class RenderTexture {

	public int width { get => ReadSize("width"); set => WriteSize("width", value); }
	public int height { get => ReadSize("height"); set => WriteSize("height", value); }
	public void Resize(int width, int height) {
		if (width < 1 || height < 1 || width > 16384 || height > 16384) throw new ArgumentOutOfRangeException(nameof(width));
		if (!NativeAssetSettingsAPI.Write(id, MaterialParameterID.FromName("size").value, "size",
			new NativeMaterialParameterValue { type = NativeMaterialParameterValueType.Vector2, x = width, y = height }))
			throw new InvalidOperationException("RenderTextureのサイズを変更できません");
	}
	private int ReadSize(string name) => NativeAssetSettingsAPI.Read(id, MaterialParameterID.FromName(name).value, out var value)
		? value.intValue : throw new InvalidOperationException("RenderTextureを読み込めません");
	private void WriteSize(string name, int value) {
		if (value < 1 || value > 16384) throw new ArgumentOutOfRangeException(nameof(value));
		if (!NativeAssetSettingsAPI.Write(id, MaterialParameterID.FromName(name).value, name,
			new NativeMaterialParameterValue { type = NativeMaterialParameterValueType.Int, intValue = value }))
			throw new InvalidOperationException("RenderTextureのサイズを変更できません");
	}
}
