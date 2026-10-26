using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static unsafe class ParticleSettingsTests {

	private static string saved = "";
	private static bool cleared;

	internal static void Run() {
		var alive = NativeAPI.IsAlive;
		var instance = NativeAPI.GetComponentInstanceID;
		var copy = NativeAPI.CopyParticleSystemSettings;
		var set = NativeAPI.SetParticleSystemSettings;
		var clear = NativeAPI.ClearParticlePaint;
		NativeAPI.IsAlive = &Alive;
		NativeAPI.GetComponentInstanceID = &Instance;
		NativeAPI.CopyParticleSystemSettings = &Copy;
		NativeAPI.SetParticleSystemSettings = &Set;
		NativeAPI.ClearParticlePaint = &Clear;
		try {
			saved = """{"looping":true,"unknown":{"preserved":7},"source":{"kind":"Analytic","renderer":{"kind":"Scene","sourceAsset":"00000000000000000000000000000012","localFileId":"0000000000000042"}},"phases":[{"name":"炎","id":"0000000000000001","modules":[{"id":"Kept","instanceID":"0000000000000002","enabled":false,"params":{"number":3}}]}]}""";
			NativeEntity entity = new() { world = new() { index = 81, generation = 1 }, index = 1, generation = 2 };
			var owner = GameObject.FromNative(entity)!;
			var particles = ParticleSystem.FromEntity(owner);
			var settings = particles.GetSettings();
			Check(settings.phases[0].name == "炎" && settings.source.renderer.localFileID.value == 0x42);
			settings.source.kind = ParticleSourceKind.MeshSurface;
			settings.emitter.emitCount = 8;
			settings.emitter.speed = 2;
			settings.emitter.shape = ParticleEmitterShape.Point;
			settings.emitter.pointDirection = Vector3.down;
			settings.emitter.SetOffsetRange(new Vector3(-1, 0, -1), new Vector3(1, 0, 1));
			settings.source.subMeshes = new uint[] { 1, 4 };
			settings.source.maskFromVertexColors = true;
			settings.surface.lit = true;
			settings.feedback.AddEvent(ParticleEventKind.Collision, settings.source.renderer).count = 3;
			settings.feedback.paint.enabled = true;
			var collision = settings.phases[0].AddCollision();
			collision.enabled = true;
			collision.radius = 0.25f;
			settings.phases[0].lifetime = 3;
			var custom = settings.phases[0].AddCustomData().customData1;
			custom.mode = ParticleParameterMode.OverLifetime;
			custom.start = new Vector4(0, 1, 0, 1);
			custom.end = new Vector4(1, 0, 0, 1);
			particles.SetSettings(settings);
			var value = JsonNode.Parse(saved)!;
			Check(value["unknown"]!["preserved"]!.GetValue<int>() == 7);
			Check(value["phases"]![0]!["modules"]![0]!["id"]!.GetValue<string>() == "Kept");
			Check(!value["phases"]![0]!["modules"]![0]!["enabled"]!.GetValue<bool>());
			Check(value["source"]!["renderer"]!["localFileId"]!.GetValue<string>() == "0000000000000042");
			Check(value["source"]!["subMeshes"]![1]!.GetValue<uint>() == 4 && settings.phases[0].GetCollision()!.radius == 0.25f);
			Check(value["feedback"]!["events"]![0]!["count"]!.GetValue<uint>() == 3 && settings.surface.lit);
			Check(settings.emitter.emitCount == 8 && settings.emitter.speed == 2 && settings.phases[0].lifetime == 3);
			Check(value["emitter"]!["pointDirection"]!["y"]!.GetValue<float>() == -1 && custom.start.y == 1 && custom.end.x == 1);
			ParticlePaintReceiver.FromEntity(owner).Clear();
			Check(cleared && Marshal.SizeOf<SplineControlPoint>() == 36 && Marshal.SizeOf<SplineSample>() == 28);
		} finally {
			NativeAPI.IsAlive = alive;
			NativeAPI.GetComponentInstanceID = instance;
			NativeAPI.CopyParticleSystemSettings = copy;
			NativeAPI.SetParticleSystemSettings = set;
			NativeAPI.ClearParticlePaint = clear;
		}
	}

	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Alive(NativeEntity entity) => entity.world.index == 81 ? 1 : 0;
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static ulong Instance(NativeEntity entity, int typeID) => 5;
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Copy(NativeEntity entity, byte* buffer, int capacity, int* written) {
		byte[] bytes = Encoding.UTF8.GetBytes(saved);
		*written = bytes.Length;
		if (buffer == null || capacity < bytes.Length) return (int)ManagedStatus.BufferTooSmall;
		bytes.CopyTo(new Span<byte>(buffer, capacity));
		return (int)ManagedStatus.Ok;
	}
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Set(NativeEntity entity, byte* buffer, int size) {
		saved = Encoding.UTF8.GetString(new ReadOnlySpan<byte>(buffer, size));
		return (int)ManagedStatus.Ok;
	}
	[UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
	private static int Clear(NativeEntity entity) { cleared = true; return 1; }
	private static void Check(bool value) { if (!value) throw new InvalidOperationException("Particle settings bridge mismatch."); }
}
