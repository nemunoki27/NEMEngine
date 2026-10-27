using System.Text;
using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	ComponentSettingsTransfer class
//	複合データのSnapshotをNativeと転送する
//============================================================================
internal static unsafe class ComponentSettingsTransfer {

	internal static JsonObject Read(NativeEntity entity, int type) {
		if (NativeAPI.CopyComponentSettings == null) throw new NotSupportedException("Component設定APIが接続されていません");
		int size = NativeAPI.CopyComponentSettings(entity, type, null, 0);
		if (size < 0 || size > 16 * 1024 * 1024) throw new InvalidOperationException("Component設定を読み込めません");
		byte[] bytes = new byte[size + 1];
		fixed (byte* data = bytes) {
			int written = NativeAPI.CopyComponentSettings(entity, type, data, bytes.Length);
			if (written < 0 || written > size) throw new InvalidOperationException("Component設定を転送できません");
			return JsonNode.Parse(Encoding.UTF8.GetString(bytes, 0, written))!.AsObject();
		}
	}

	internal static void Apply(NativeEntity entity, int type, JsonObject settings) {
		if (NativeAPI.ApplyComponentSettings == null) throw new NotSupportedException("Component設定APIが接続されていません");
		byte[] bytes = Encoding.UTF8.GetBytes(settings.ToJsonString());
		fixed (byte* data = bytes) {
			if (NativeAPI.ApplyComponentSettings(entity, type, data, bytes.Length) == 0)
				throw new InvalidOperationException("Component設定を変更できません");
		}
	}
}
