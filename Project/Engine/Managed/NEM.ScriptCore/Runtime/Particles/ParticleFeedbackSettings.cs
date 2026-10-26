using System.Text.Json.Nodes;

namespace NEMEngine;

public enum ParticleEventKind { Collision, Death, Time, PhaseTransition }

public sealed class ParticleFeedbackSettings : ParticleSettingsObject {

	internal ParticleFeedbackSettings(JsonObject values) : base(values) { }
	public uint eventCapacity { get => Read("eventCapacity", 4096u); set => Write("eventCapacity", value); }
	public ParticlePaintSettings paint => new(Child("paint"));
	public ParticleImpactDecalSettings decal => new(Child("decal"));
	public IReadOnlyList<ParticleEventRouteSettings> events => (values["events"] as JsonArray ?? new JsonArray())
		.OfType<JsonObject>().Select(value => new ParticleEventRouteSettings(value)).ToArray();

	public ParticleEventRouteSettings AddEvent(ParticleEventKind kind, ParticleEntityReference target) {
		if (values["events"] is not JsonArray array) values["events"] = array = new JsonArray();
		if (array.Count >= 32) throw new InvalidOperationException("Particle event routes are limited to 32.");
		var value = new JsonObject();
		array.Add(value);
		var route = new ParticleEventRouteSettings(value) { kind = kind, target = target };
		return route;
	}

	public void RemoveEventAt(int index) => ((JsonArray)values["events"]!).RemoveAt(index);
}

public sealed class ParticleEventRouteSettings : ParticleSettingsObject {

	internal ParticleEventRouteSettings(JsonObject values) : base(values) { }
	public bool enabled { get => Read("enabled", true); set => Write("enabled", value); }
	public ParticleEventKind kind { get => ReadEnum("kind", ParticleEventKind.Collision); set => WriteEnum("kind", value); }
	public ParticleEntityReference target { get => ParticleEntityReference.Read(values["target"]); set => values["target"] = value.ToJson(); }
	public uint phase { get => Read("phase", uint.MaxValue); set => Write("phase", value); }
	public uint count { get => Read("count", 1u); set => Write("count", value); }
	public float time { get => Read("time", 0.5f); set => Write("time", value); }
	public float probability { get => Read("probability", 1f); set => Write("probability", value); }
	public bool inheritPosition { get => Read("inheritPosition", true); set => Write("inheritPosition", value); }
	public bool inheritNormal { get => Read("inheritNormal", true); set => Write("inheritNormal", value); }
	public bool inheritVelocity { get => Read("inheritVelocity", false); set => Write("inheritVelocity", value); }
	public bool inheritColor { get => Read("inheritColor", true); set => Write("inheritColor", value); }
	public bool inheritSeed { get => Read("inheritSeed", true); set => Write("inheritSeed", value); }
}

public sealed class ParticlePaintSettings : ParticleSettingsObject {

	internal ParticlePaintSettings(JsonObject values) : base(values) { }
	public bool enabled { get => Read("enabled", false); set => Write("enabled", value); }
	public float radius { get => Read("radius", 0.1f); set => Write("radius", value); }
	public float hardness { get => Read("hardness", 0.8f); set => Write("hardness", value); }
	public Color4 color { get => ReadColor("color", Color4.white); set => WriteColor("color", value); }
}

public sealed class ParticleImpactDecalSettings : ParticleSettingsObject {

	internal ParticleImpactDecalSettings(JsonObject values) : base(values) { }
	public bool enabled { get => Read("enabled", false); set => Write("enabled", value); }
	public AssetGUID material { get => ReadAsset("material"); set => WriteAsset("material", value); }
	public Vector3 size { get => ReadVector3("size", new(0.2f, 0.2f, 0.1f)); set => WriteVector3("size", value); }
	public Color4 color { get => ReadColor("color", Color4.white); set => WriteColor("color", value); }
	public float lifetime { get => Read("lifetime", 5f); set => Write("lifetime", value); }
	public float fadeDuration { get => Read("fadeDuration", 1f); set => Write("fadeDuration", value); }
	public uint capacity { get => Read("capacity", 1024u); set => Write("capacity", value); }
	public uint receiverRenderingLayerMask { get => Read("receiverRenderingLayerMask", 0x00ffffffu); set => Write("receiverRenderingLayerMask", value); }
}
