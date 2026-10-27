namespace NEMEngine;

//============================================================================
//	NativeCameraPassAPI class
//	Camera単位のPass操作をNativeへ渡す
//============================================================================
internal static unsafe class NativeCameraPassAPI {

	internal static RenderPassesPass Find(NativeEntity camera, int type, string name) {
		if (NativeAPI.ResolveCameraRenderPass == null) return default;
		byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name);
		NativeRenderPassHandle handle = default;
		fixed (byte* data = bytes) return NativeAPI.ResolveCameraRenderPass(camera, type, data, &handle) != 0 ? new(handle) : default;
	}

	internal static bool ValidateRenderFeaturePassValue(NativeRenderPassHandle handle) => Action(handle, 0);
	internal static bool WriteRenderFeaturePassEnabled(NativeRenderPassHandle handle, bool enabled) => Action(handle, 1, argument: enabled ? 1ul : 0);
	internal static bool ReadEnabled(NativeRenderPassHandle handle) => Action(handle, 2);
	internal static bool ClearRenderFeaturePassParameterValue(NativeRenderPassHandle handle, ulong id) => Action(handle, 5, argument: id);
	internal static bool ResetRenderFeaturePassValue(NativeRenderPassHandle handle) => Action(handle, 6);

	internal static bool WriteRenderFeaturePassParameter(NativeRenderPassHandle handle, ulong id, string name, NativeMaterialParameterValue value) {
		if (NativeAPI.SetCameraRenderPassParameter == null) return false;
		byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name);
		fixed (byte* data = bytes) return NativeAPI.SetCameraRenderPassParameter(&handle, id, data, &value) != 0;
	}

	internal static bool ReadRenderFeaturePassParameter(NativeRenderPassHandle handle, ulong id, out NativeMaterialParameterValue value) {
		NativeMaterialParameterValue result = default;
		bool found = NativeAPI.GetCameraRenderPassParameter != null && NativeAPI.GetCameraRenderPassParameter(&handle, id, &result) != 0;
		value = result;
		return found;
	}

	internal static bool Action(NativeRenderPassHandle handle, int action, string? name = null, ulong argument = 0) {
		if (NativeAPI.CameraRenderPassAction == null) return false;
		byte[] bytes = ManagedUTF8Transfer.GetNullTerminatedBytes(name ?? string.Empty);
		fixed (byte* data = bytes) return NativeAPI.CameraRenderPassAction(&handle, action, data, argument) != 0;
	}
}

//============================================================================
//	CameraRenderPasses class
//	Camera固有のPassと共通出力設定を操作する
//============================================================================
public readonly struct CameraRenderPasses {

	private readonly Component? owner;
	private readonly int type;
	internal CameraRenderPasses(Component? owner, int type) { this.owner = owner; this.type = type; }
	public RenderPassesPass FindPass(string name) {
		ArgumentException.ThrowIfNullOrEmpty(name);
		return NativeCameraPassAPI.Find(owner?.native ?? default, type, name);
	}
	public bool SetGroupEnabled(string name, bool enabled) => NativeCameraPassAPI.Action(Root.handle, 3, name, enabled ? 1ul : 0);
	public bool SetViewEnabled(bool gameView, bool sceneView) => NativeCameraPassAPI.Action(Root.handle, 8, argument: (gameView ? 1ul : 0) | (sceneView ? 2ul : 0));
	public bool ResetAll() => NativeCameraPassAPI.Action(Root.handle, 7);
	private RenderPassesPass Root => NativeCameraPassAPI.Find(owner?.native ?? default, type, string.Empty);
}

public sealed partial class PerspectiveCamera {
	public CameraRenderPasses renderPasses => new(this, componentTypeID);
}

public sealed partial class OrthographicCamera {
	public CameraRenderPasses renderPasses => new(this, componentTypeID);
}
