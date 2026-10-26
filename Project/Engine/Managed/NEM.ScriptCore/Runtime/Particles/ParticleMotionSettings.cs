using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed class ParticlePhaseSettings : ParticleSettingsObject {

	internal ParticlePhaseSettings(JsonObject values) : base(values) { }
	public string name { get => Read("name", "Phase"); set => Write("name", value); }
	public float lifetime {
		get => (values["lifetime"] as JsonObject)?["constant"]?.GetValue<float>() ?? 1;
		set => values["lifetime"] = new JsonObject { ["type"] = "Constant", ["constant"] = value, ["min"] = value, ["max"] = value };
	}
	public int moduleCount => (values["modules"] as JsonArray)?.Count ?? 0;
	public void ClearModules() => values["modules"] = new JsonArray();
	public ParticleCurlNoiseSettings AddCurlNoise() => new(AddModule("CurlNoise"));
	public ParticleGravitySettings AddGravity() => new(AddModule("GravityForce"));
	public ParticleSizeSettings AddSizeOverLifetime() => new(AddModule("SizeOverLifetime"));
	public ParticleScaleSettings AddScaleOverLifetime() => new(AddModule("ScaleOverLifetime"));
	public ParticleColorSettings AddColorOverLifetime() => new(AddModule("ColorOverLifetime"));
	public ParticleCustomDataSettings AddCustomData() => new(AddModule("CustomData"));
	public AssetGUID baseColorTexture {
		get => AssetGUID.Parse((values["materialSettings"] as JsonObject)?["baseColorTexture"]?.GetValue<string>());
		set => Child("materialSettings")["baseColorTexture"] = value.isValid ? value.ToString() : "";
	}
	public ParticleForceFieldSettings AddForceField() => new(AddModule("ForceField"));
	public ParticleVectorFieldSettings AddVectorField() => new(AddModule("VectorField"));
	public ParticleSplineMovementSettings AddSplineMovement() => new(AddModule("SplineMovement"));
	public ParticleCollisionSettings AddCollision() => new(AddModule("Collision"));
	public ParticleForceFieldSettings? GetForceField(int occurrence = 0) => FindModule("ForceField", occurrence) is JsonObject value ? new(value) : null;
	public ParticleVectorFieldSettings? GetVectorField(int occurrence = 0) => FindModule("VectorField", occurrence) is JsonObject value ? new(value) : null;
	public ParticleSplineMovementSettings? GetSplineMovement(int occurrence = 0) => FindModule("SplineMovement", occurrence) is JsonObject value ? new(value) : null;
	public ParticleCollisionSettings? GetCollision(int occurrence = 0) => FindModule("Collision", occurrence) is JsonObject value ? new(value) : null;
	public void RemoveModuleAt(int index) => ((JsonArray)values["modules"]!).RemoveAt(index);

	private JsonObject AddModule(string id) {
		if (values["modules"] is not JsonArray modules) values["modules"] = modules = new JsonArray();
		var parameters = new JsonObject();
		modules.Add(new JsonObject { ["id"] = id, ["instanceID"] = Guid.NewGuid().ToString("N")[..16], ["params"] = parameters });
		return parameters;
	}

	private JsonObject? FindModule(string id, int occurrence) {
		if (occurrence < 0) throw new ArgumentOutOfRangeException(nameof(occurrence));
		foreach (var module in (values["modules"] as JsonArray ?? new JsonArray()).OfType<JsonObject>()) {
			if (module["id"]?.GetValue<string>() != id || occurrence-- != 0) continue;
			if (module["params"] is not JsonObject parameters) module["params"] = parameters = new JsonObject();
			return parameters;
		}
		return null;
	}
}

public sealed class ParticleForceFieldSettings : ParticleSettingsObject {

	internal ParticleForceFieldSettings(JsonObject values) : base(values) { }
	public ParticleForceFieldType type { get => (ParticleForceFieldType)Read("type", 0u); set => Write("type", (uint)value); }
	public Vector3 center { get => ReadVector3("center", Vector3.zero); set => WriteVector3("center", value); }
	public Vector3 axis { get => ReadVector3("axis", Vector3.up); set => WriteVector3("axis", value); }
	public float strength { get => Read("strength", 1f); set => Write("strength", value); }
	public float range { get => Read("range", 10f); set => Write("range", value); }
	public float falloff { get => Read("falloff", 1f); set => Write("falloff", value); }
	public uint layerMask { get => Read("layerMask", uint.MaxValue); set => Write("layerMask", value); }
	public bool useSceneFields { get => Read("useSceneFields", true); set => Write("useSceneFields", value); }
}

public sealed class ParticleVectorFieldSettings : ParticleSettingsObject {

	internal ParticleVectorFieldSettings(JsonObject values) : base(values) { }
	public float intensity { get => Read("intensity", 1f); set => Write("intensity", value); }
	public float tightness { get => Read("tightness", 0f); set => Write("tightness", value); }
	public uint layerMask { get => Read("layerMask", uint.MaxValue); set => Write("layerMask", value); }
}

public sealed class ParticleSplineMovementSettings : ParticleSettingsObject {

	internal ParticleSplineMovementSettings(JsonObject values) : base(values) { }
	public ParticleEntityReference spline { get => ParticleEntityReference.Read(values["spline"]); set => values["spline"] = value.ToJson(); }
	public float speed { get => Read("speed", 1f); set => Write("speed", value); }
	public float startDistance { get => Read("startDistance", 0f); set => Write("startDistance", value); }
	public float orbitRadius { get => Read("orbitRadius", 0f); set => Write("orbitRadius", value); }
	public float orbitTurnsPerMeter { get => Read("orbitTurnsPerMeter", 0f); set => Write("orbitTurnsPerMeter", value); }
	public bool loop { get => Read("loop", true); set => Write("loop", value); }
	public bool orientToTangent { get => Read("orientToTangent", true); set => Write("orientToTangent", value); }
}

public sealed class ParticleCollisionSettings : ParticleSettingsObject {

	internal ParticleCollisionSettings(JsonObject values) : base(values) { }
	public bool enabled { get => Read("enabled", false); set => Write("enabled", value); }
	public bool sceneColliders { get => Read("sceneColliders", true); set => Write("sceneColliders", value); }
	public bool plane { get => Read("plane", false); set => Write("plane", value); }
	public bool depth { get => Read("depth", false); set => Write("depth", value); }
	public bool heightmap { get => Read("heightmap", false); set => Write("heightmap", value); }
	public bool includeTriggers { get => Read("includeTriggers", false); set => Write("includeTriggers", value); }
	public Vector3 planeNormal { get => ReadVector3("planeNormal", Vector3.up); set => WriteVector3("planeNormal", value); }
	public float planeDistance { get => Read("planeDistance", 0f); set => Write("planeDistance", value); }
	public float radius { get => Read("radius", 0.05f); set => Write("radius", value); }
	public float bounce { get => Read("bounce", 0.5f); set => Write("bounce", value); }
	public float friction { get => Read("friction", 0f); set => Write("friction", value); }
	public float depthThickness { get => Read("depthThickness", 0.2f); set => Write("depthThickness", value); }
	public uint layerMask { get => Read("layerMask", uint.MaxValue); set => Write("layerMask", value); }
	public ParticleCollisionResponse response { get => (ParticleCollisionResponse)Read("response", 0u); set => Write("response", (uint)value); }
	public ParticleEntityReference depthCamera { get => ParticleEntityReference.Read(values["depthCamera"]); set => values["depthCamera"] = value.ToJson(); }
}
