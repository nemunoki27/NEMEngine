using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static unsafe class RendererRenderSettingsTests {

	private static string saved = "";
	private static int expectedType;
	private static bool reject;

	internal static void Run() {

		var alive = NativeAPI.IsAlive;
		var instance = NativeAPI.GetComponentInstanceID;
		var copy = NativeAPI.CopyComponentSettings;
		var apply = NativeAPI.ApplyComponentSettings;
		NativeAPI.IsAlive = &Alive;
		NativeAPI.GetComponentInstanceID = &Instance;
		NativeAPI.CopyComponentSettings = &Copy;
		NativeAPI.ApplyComponentSettings = &Apply;
		try {
			NativeEntity entity = new() { world = new() { index = 81, generation = 1 }, index = 1, generation = 2 };
			var owner = GameObject.FromNative(entity)!;
			var mesh = MeshRenderer.FromEntity(owner);
			var primitive = PrimitiveRenderer.FromEntity(owner);
			var sprite = SpriteRenderer.FromEntity(owner);
			var text = TextRenderer.FromEntity(owner);
			var line = LineRenderer.FromEntity(owner);
			var particle = ParticleSystem.FromEntity(owner);
			Check(14, mesh.GetRenderSettings, mesh.SetRenderSettings);
			Check(20, primitive.GetRenderSettings, primitive.SetRenderSettings);
			Check(29, sprite.GetRenderSettings, sprite.SetRenderSettings);
			Check(30, text.GetRenderSettings, text.SetRenderSettings);
			Check(13, line.GetRenderSettings, line.SetRenderSettings);
			Check(8, particle.GetRenderSettings, particle.SetRenderSettings);
		} finally {
			NativeAPI.IsAlive = alive;
			NativeAPI.GetComponentInstanceID = instance;
			NativeAPI.CopyComponentSettings = copy;
			NativeAPI.ApplyComponentSettings = apply;
		}
	}

	private static void Check(int type, Func<RendererRenderSettings> read, Action<RendererRenderSettings> apply) {

		expectedType = type;
		reject = false;
		saved = new JsonObject { ["renderSettings"] = new RendererRenderSettings().values.DeepClone() }.ToJsonString();
		var settings = read();
		settings.inheritMaterial = false;
		settings.fillMode = RendererFillMode.WireFrame;
		settings.depthTest = false;
		settings.depthWrite = true;
		settings.stencilEnabled = true;
		settings.separateBackFace = true;
		settings.stencilReference = 237;
		settings.frontFace.pass = RendererStencilOperation.REPLACE;
		settings.backFace.comparison = RendererDepthComparison.NOT_EQUAL;
		apply(settings);
		var result = read();
		if (result.inheritMaterial || result.fillMode != RendererFillMode.WireFrame || result.depthTest || !result.depthWrite ||
			!result.stencilEnabled || !result.separateBackFace || result.stencilReference != 237 ||
			result.frontFace.pass != RendererStencilOperation.REPLACE || result.backFace.comparison != RendererDepthComparison.NOT_EQUAL)
			throw new InvalidOperationException("Renderer設定の転送結果が一致しません");
		reject = true;
		try { apply(settings); } catch (InvalidOperationException) { return; }
		throw new InvalidOperationException("Nativeの設定失敗が通知されません");
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Alive(NativeEntity entity) => 1;
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static ulong Instance(NativeEntity entity, int type) => 5;
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Copy(NativeEntity entity, int type, byte* buffer, int capacity) {

		if (type != expectedType) return -1;
		var bytes = Encoding.UTF8.GetBytes(saved);
		if (buffer == null) return bytes.Length;
		if (capacity < bytes.Length) return -1;
		bytes.CopyTo(new Span<byte>(buffer, capacity));
		return bytes.Length;
	}
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Apply(NativeEntity entity, int type, byte* buffer, int size) {

		if (reject || type != expectedType) return 0;
		var patch = JsonNode.Parse(Encoding.UTF8.GetString(new ReadOnlySpan<byte>(buffer, size)))!;
		if (!patch["renderSettingsPatch"]!.GetValue<bool>()) return 0;
		saved = new JsonObject { ["renderSettings"] = patch["renderSettings"]!.DeepClone() }.ToJsonString();
		return 1;
	}
}
