using System.Text;
using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	ParticleModuleDefaults class
//	追加するModuleの初期値をNativeから取得する
//============================================================================
internal static unsafe class ParticleModuleDefaults {

	internal static JsonObject Read(string id) {
		if (NativeAPI.CopyParticleModuleDefaults == null) return new JsonObject();
		byte[] name = ManagedUTF8Transfer.GetNullTerminatedBytes(id);
		fixed (byte* input = name) {
			int size = NativeAPI.CopyParticleModuleDefaults(input, null, 0);
			if (size < 0 || size > 16 * 1024 * 1024) throw new ArgumentException($"Moduleを追加できません: {id}");
			byte[] buffer = new byte[size + 1];
			fixed (byte* output = buffer) {
				int written = NativeAPI.CopyParticleModuleDefaults(input, output, buffer.Length);
				if (written != size) throw new InvalidOperationException("Moduleの初期値を取得できません");
				return JsonNode.Parse(Encoding.UTF8.GetString(buffer, 0, written))!.AsObject();
			}
		}
	}
}
