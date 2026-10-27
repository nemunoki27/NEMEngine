using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	ParticleSystemSettings class
//	形状とPhaseの構成を編集する
//============================================================================
public sealed partial class ParticleSystemSettings {

	public PrimitiveRenderSpace space { get => ReadEnum("space", PrimitiveRenderSpace.World3D); set => WriteEnum("space", value); }
	public RenderPhase queue { get => ReadEnum("queue", RenderPhase.Transparent); set => WriteEnum("queue", value); }
	public AssetGUID model { get => ReadAsset("model"); set => WriteAsset("model", value); }
	public PlaneGeometrySettings plane => new(Child("plane"));
	public CrossPlaneGeometrySettings crossPlane => new(Child("crossPlane"));
	public RingGeometrySettings ring => new(Child("ring"));
	public CylinderGeometrySettings cylinder => new(Child("cylinder"));
	public SphereGeometrySettings sphere => new(Child("sphere"));
	public HemisphereGeometrySettings hemisphere => new(Child("hemisphere"));
	public CubeGeometrySettings cube => new(Child("cube"));
	public ParticleTrailSettings trail => new(Child("trail"));
	public ParticleAxes billboardAxes {
		get => (values["billboardAxes"] as JsonArray ?? new JsonArray()).Aggregate(ParticleAxes.None,
			(result, item) => result | (Enum.TryParse<ParticleAxes>(item?.GetValue<string>(), out var axis) ? axis : ParticleAxes.None));
		set => values["billboardAxes"] = new JsonArray(new[] { ParticleAxes.X, ParticleAxes.Y, ParticleAxes.Z }
			.Where(axis => (value & axis) != 0).Select(axis => (JsonNode?)JsonValue.Create(axis.ToString())).ToArray());
	}
	public ParticlePhaseSettings AddPhase(string name = "Phase") {
		if (values["phases"] is not JsonArray phases) values["phases"] = phases = new JsonArray();
		var phase = new JsonObject { ["name"] = name, ["id"] = Guid.NewGuid().ToString("N")[..16],
			["modules"] = new JsonArray(), ["lifetime"] = new JsonObject { ["type"] = "Constant", ["constant"] = 1f, ["min"] = 1f, ["max"] = 1f } };
		phases.Add(phase);
		return new(phase);
	}
	public void RemovePhaseAt(int index) {
		var phases = (JsonArray)values["phases"]!;
		if (phases.Count <= 1) throw new InvalidOperationException("Phaseは1つ以上必要です");
		phases.RemoveAt(index);
	}
	public void MovePhase(int source, int destination) => Move((JsonArray)values["phases"]!, source, destination);
	internal static void Move(JsonArray array, int source, int destination) {
		if ((uint)source >= array.Count || (uint)destination >= array.Count) throw new ArgumentOutOfRangeException(nameof(destination));
		var value = array[source]; array.RemoveAt(source); array.Insert(destination, value);
	}
}

public sealed partial class ParticlePhaseSettings {

	public UUID phaseID => UUID.Parse(Read("id", ""));
	public ParticleValueSettings<float> lifetimeSettings => new(Child("lifetime"));
	public ParticleLifeEndMode lifeEndMode { get => ReadEnum("lifeEndMode", ParticleLifeEndMode.Kill); set => WriteEnum("lifeEndMode", value); }
	public AssetGUID material { get => ReadAsset("material"); set => WriteAsset("material", value); }
	public ParticleMaterialSettings materialSettings => new(Child("materialSettings"));
	public ParticlePhaseParentSettings parent => new(Child("parentSettings"));
	public void MoveModule(int source, int destination) => ParticleSystemSettings.Move((JsonArray)values["modules"]!, source, destination);
	public void SetModuleEnabled(int index, bool enabled) => ((JsonArray)values["modules"]!)[index]!["enabled"] = enabled;
	public bool GetModuleEnabled(int index) => ((JsonArray)values["modules"]!)[index]!["enabled"]?.GetValue<bool>() ?? true;
}

public sealed class ParticleTrailSettings : SettingsObject {

	internal ParticleTrailSettings(JsonObject values) : base(values) { }
	public bool enabled { get => Read("enabled", false); set => Write("enabled", value); }
	public bool drawSource { get => Read("drawSource", true); set => Write("drawSource", value); }
	public bool keepAfterParticleDeath { get => Read("keepAfterParticleDeath", false); set => Write("keepAfterParticleDeath", value); }
	public ParticleTrailMode mode { get => ReadEnum("mode", ParticleTrailMode.Particle); set => WriteEnum("mode", value); }
	public ParticleTrailTextureMode textureMode { get => ReadEnum("textureMode", ParticleTrailTextureMode.Stretch); set => WriteEnum("textureMode", value); }
	public float tileLength { get => Read("tileLength", 0.2f); set => Write("tileLength", value); }
	public uint maxTrails { get => Read("maxTrails", 1024u); set => Write("maxTrails", value); }
	public uint memoryBudgetMB { get => Read("memoryBudgetMB", 32u); set => Write("memoryBudgetMB", value); }
	public int maxPoints { get => Read("maxPoints", 16); set => Write("maxPoints", value); }
	public float minDistance { get => Read("minDistance", 0.05f); set => Write("minDistance", value); }
	public float startWidth { get => Read("startWidth", 0.1f); set => Write("startWidth", value); }
	public float endWidth { get => Read("endWidth", 0.1f); set => Write("endWidth", value); }
	public Color4 startColor { get => ReadColor("startColor", Color4.white); set => WriteColor("startColor", value); }
	public Color4 endColor { get => ReadColor("endColor", new(1, 1, 1, 0)); set => WriteColor("endColor", value); }
	public float pointLifetime { get => Read("pointLifetime", 0.8f); set => Write("pointLifetime", value); }
	public AssetGUID material { get => ReadAsset("material"); set => WriteAsset("material", value); }
	public ParticleMaterialSettings materialSettings => new(Child("materialSettings"));
}

public sealed class ParticleEmitterCircleSettings : SettingsObject {

	internal ParticleEmitterCircleSettings(JsonObject values) : base(values) { }
	public float radius { get => Read("circleRadius", 1f); set => Write("circleRadius", value); }
	public float angleMinimum { get => Read("circleAngleMin", 0f); set => Write("circleAngleMin", value); }
	public float angleMaximum { get => Read("circleAngleMax", 360f); set => Write("circleAngleMax", value); }
	public bool clockwise { get => Read("circleClockwise", false); set => Write("circleClockwise", value); }
	public ParticleEmitterSpawnMode spawnMode { get => ReadEnum("circleSpawnMode", ParticleEmitterSpawnMode.Random); set => WriteEnum("circleSpawnMode", value); }
	public float stepAngle { get => Read("circleStepAngle", 10f); set => Write("circleStepAngle", value); }
	public ParticleEmitterVelocityMode velocityMode { get => ReadEnum("circleVelocityMode", ParticleEmitterVelocityMode.Normal); set => WriteEnum("circleVelocityMode", value); }
	public bool usePrevSegmentDirectionOnWrap { get => Read("circleUsePrevSegmentDirectionOnWrap", false); set => Write("circleUsePrevSegmentDirectionOnWrap", value); }
}

public sealed partial class ParticleEmitterSettings {
	public ParticleEmitterCircleSettings circle => new(values);
}
