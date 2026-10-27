using System.Text.Json.Nodes;

namespace NEMEngine;

public enum ParticleEmitterSpawnMode { Random, EvenPerFrame, Progressive }
public enum ParticleEmitterVelocityMode { Normal, NextPoint, PrePoint }

public sealed class ParticleEmitterBoxSettings : SettingsObject {

	internal ParticleEmitterBoxSettings(JsonObject values) : base(values) { }
	public Vector3 size { get => ReadVector3("boxSize", Vector3.zero); set => WriteVector3("boxSize", value); }
	public bool facePosX { get => Read("boxFacePosX", false); set => Write("boxFacePosX", value); }
	public bool faceNegX { get => Read("boxFaceNegX", false); set => Write("boxFaceNegX", value); }
	public bool facePosY { get => Read("boxFacePosY", false); set => Write("boxFacePosY", value); }
	public bool faceNegY { get => Read("boxFaceNegY", false); set => Write("boxFaceNegY", value); }
	public bool facePosZ { get => Read("boxFacePosZ", false); set => Write("boxFacePosZ", value); }
	public bool faceNegZ { get => Read("boxFaceNegZ", false); set => Write("boxFaceNegZ", value); }
}

public sealed class ParticleEmitterConeSettings : SettingsObject {

	internal ParticleEmitterConeSettings(JsonObject values) : base(values) { }
	public float angle { get => Read("coneAngle", 0f); set => Write("coneAngle", value); }
	public float radius { get => Read("coneRadius", 0f); set => Write("coneRadius", value); }
}

public sealed class ParticleEmitterSphereSettings : SettingsObject {

	internal ParticleEmitterSphereSettings(JsonObject values) : base(values) { }
	public float radius { get => Read("sphereRadius", 0f); set => Write("sphereRadius", value); }
}

public sealed class ParticleEmitterRectSettings : SettingsObject {

	internal ParticleEmitterRectSettings(JsonObject values) : base(values) { }
	public Vector2 size { get => ReadVector2("rectSize", Vector2.zero); set => WriteVector2("rectSize", value); }
	public bool edgePosX { get => Read("rectEdgePosX", false); set => Write("rectEdgePosX", value); }
	public bool edgeNegX { get => Read("rectEdgeNegX", false); set => Write("rectEdgeNegX", value); }
	public bool edgePosY { get => Read("rectEdgePosY", false); set => Write("rectEdgePosY", value); }
	public bool edgeNegY { get => Read("rectEdgeNegY", false); set => Write("rectEdgeNegY", value); }
}

public sealed class ParticleEmitterTorusSettings : SettingsObject {

	internal ParticleEmitterTorusSettings(JsonObject values) : base(values) { }
	public float radius { get => Read("torusRadius", 0f); set => Write("torusRadius", value); }
	public float thickness { get => Read("torusThickness", 0f); set => Write("torusThickness", value); }
}

public sealed partial class ParticleEmitterSettings {

	public ParticleEmitterBoxSettings box => new(values);
	public ParticleEmitterConeSettings cone => new(values);
	public ParticleEmitterSphereSettings sphere => new(values);
	public ParticleEmitterRectSettings rect => new(values);
	public ParticleEmitterTorusSettings torus => new(values);
	public ParticleValueSettings<uint> emissionCount => new(Child("emitCount"));
	public ParticleValueSettings<float> startSpeed => new(Child("speed"));
	public ParticleValueSettings<Vector3> startOffset => new(Child("emitOffset"));
}
