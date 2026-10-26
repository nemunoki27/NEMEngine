using System.Text.Json.Nodes;
using System.Text;

namespace NEMEngine;

public sealed class ParticleSystemSettings : ParticleSettingsObject {

	internal ParticleSystemSettings(JsonObject values) : base(values) { }
	public bool looping { get => Read("looping", true); set => Write("looping", value); }
	public ParticleEmitterSettings emitter => new(Child("emitter"));
	public AssetGUID material { get => ReadAsset("material"); set => WriteAsset("material", value); }
	public BlendMode blendMode { get => ReadEnum("blendMode", BlendMode.Add); set => WriteEnum("blendMode", value); }
	public PrimitiveType shape { get => ReadEnum("shape", PrimitiveType.Plane); set => WriteEnum("shape", value); }
	public bool billboard {
		get => (values["billboardAxes"] as JsonArray)?.Count > 0;
		set => values["billboardAxes"] = value ? new JsonArray("X", "Y", "Z") : new JsonArray();
	}
	public ParticleSourceSettings source => new(Child("source"));
	public ParticleSurfaceSettings surface => new(Child("surface"));
	public ParticleFeedbackSettings feedback => new(Child("feedback"));
	public IReadOnlyList<ParticlePhaseSettings> phases => (values["phases"] as JsonArray ?? new JsonArray())
		.OfType<JsonObject>().Select(value => new ParticlePhaseSettings(value)).ToArray();
}

public sealed partial class ParticleSystem {

	// 変更はSetSettingsで一括適用する
	public unsafe ParticleSystemSettings GetSettings() {
		if (NativeAPI.CopyParticleSystemSettings == null) throw new NotSupportedException("Particle settings getter is not connected.");
		int needed = 0;
		int status = NativeAPI.CopyParticleSystemSettings(native, null, 0, &needed);
		if (status != (int)ManagedStatus.BufferTooSmall || needed <= 0 || needed > 16 * 1024 * 1024)
			throw new InvalidOperationException($"Particle settings cannot be read: {(ManagedStatus)status}");
		byte[] bytes = new byte[needed];
		int written = 0;
		fixed (byte* data = bytes) {
			status = NativeAPI.CopyParticleSystemSettings(native, data, bytes.Length, &written);
		}
		if (status != (int)ManagedStatus.Ok || written < 0 || written > bytes.Length)
			throw new InvalidOperationException($"Particle settings cannot be read: {(ManagedStatus)status}");
		var root = JsonNode.Parse(Encoding.UTF8.GetString(bytes.AsSpan(0, written))) as JsonObject;
		return new(root ?? throw new InvalidOperationException("Particle settings are invalid."));
	}

	public unsafe void SetSettings(ParticleSystemSettings settings) {
		ArgumentNullException.ThrowIfNull(settings);
		if (NativeAPI.SetParticleSystemSettings == null) throw new NotSupportedException("Particle settings setter is not connected.");
		byte[] bytes = Encoding.UTF8.GetBytes(settings.values.ToJsonString());
		fixed (byte* data = bytes) {
			int status = NativeAPI.SetParticleSystemSettings(native, data, bytes.Length);
			if (status != (int)ManagedStatus.Ok)
				throw new InvalidOperationException($"Particle settings cannot be applied: {(ManagedStatus)status}");
		}
	}
}

public sealed partial class ParticlePaintReceiver {

	public unsafe void Clear() {
		if (NativeAPI.ClearParticlePaint == null || NativeAPI.ClearParticlePaint(native) == 0)
			throw new InvalidOperationException("Particle paint cannot be cleared.");
	}
}
