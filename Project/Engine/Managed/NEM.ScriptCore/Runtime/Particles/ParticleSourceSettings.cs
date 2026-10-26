using System.Text.Json.Nodes;

namespace NEMEngine;

public enum ParticleSourceKind { Analytic, MeshVertex, MeshEdge, MeshSurface, Texture, CameraSurface }
public enum ParticleSourceChannel { Red, Green, Blue, Alpha, Luminance }

public sealed class ParticleSourceSettings : ParticleSettingsObject {

	internal ParticleSourceSettings(JsonObject values) : base(values) { }
	public ParticleSourceKind kind { get => ReadEnum("kind", ParticleSourceKind.Analytic); set => WriteEnum("kind", value); }
	public AssetGUID mesh { get => ReadAsset("mesh"); set => WriteAsset("mesh", value); }
	public ParticleEntityReference renderer { get => ParticleEntityReference.Read(values["renderer"]); set => values["renderer"] = value.ToJson(); }
	public uint[] subMeshes {
		get => (values["subMeshes"] as JsonArray ?? new JsonArray()).Select(node => node!.GetValue<uint>()).ToArray();
		set => values["subMeshes"] = new JsonArray((value ?? Array.Empty<uint>()).Select(item => (JsonNode?)JsonValue.Create(item)).ToArray());
	}
	public AssetGUID texture { get => ReadAsset("texture"); set => WriteAsset("texture", value); }
	public ParticleEntityReference camera { get => ParticleEntityReference.Read(values["camera"]); set => values["camera"] = value.ToJson(); }
	public Vector2 textureSize { get => ReadVector2("textureSize", new(1, 1)); set => WriteVector2("textureSize", value); }
	public float surfaceOffset { get => Read("surfaceOffset", 0f); set => Write("surfaceOffset", value); }
	public bool sequential { get => Read("sequential", false); set => Write("sequential", value); }
	public bool attachToSurface { get => Read("attachToSurface", false); set => Write("attachToSurface", value); }
	public bool inheritColor { get => Read("inheritColor", false); set => Write("inheritColor", value); }
	public float inheritVelocity { get => Read("inheritVelocity", 0f); set => Write("inheritVelocity", value); }
	public bool useMask { get => Read("useMask", false); set => Write("useMask", value); }
	public bool maskFromVertexColors { get => Read("maskFromVertexColors", false); set => Write("maskFromVertexColors", value); }
	public AssetGUID maskTexture { get => ReadAsset("maskTexture"); set => WriteAsset("maskTexture", value); }
	public ParticleSourceChannel maskChannel { get => ReadEnum("maskChannel", ParticleSourceChannel.Alpha); set => WriteEnum("maskChannel", value); }
	public float maskMinimum { get => Read("maskMinimum", 0f); set => Write("maskMinimum", value); }
	public float maskMaximum { get => Read("maskMaximum", 1f); set => Write("maskMaximum", value); }
}

public sealed class ParticleSurfaceSettings : ParticleSettingsObject {

	internal ParticleSurfaceSettings(JsonObject values) : base(values) { }
	public bool softParticles { get => Read("softParticles", false); set => Write("softParticles", value); }
	public float softDistance { get => Read("softDistance", 0.2f); set => Write("softDistance", value); }
	public bool lit { get => Read("lit", false); set => Write("lit", value); }
	public bool receiveShadows { get => Read("receiveShadows", true); set => Write("receiveShadows", value); }
	public bool receiveIBL { get => Read("receiveIBL", true); set => Write("receiveIBL", value); }
	public AssetGUID normalTexture { get => ReadAsset("normalTexture"); set => WriteAsset("normalTexture", value); }
	public float normalScale { get => Read("normalScale", 1f); set => Write("normalScale", value); }
	public float metallic { get => Read("metallic", 0f); set => Write("metallic", value); }
	public float roughness { get => Read("roughness", 0.5f); set => Write("roughness", value); }
}
