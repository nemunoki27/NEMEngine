using System.Text.Json.Nodes;

namespace NEMEngine;

// Moduleの編集値を実行用Snapshotへ保持する
public sealed partial class ParticleAlphaReferenceSettings : ParticleModuleSettings {

	internal ParticleAlphaReferenceSettings(JsonObject values) : base(values) { }

	public float startReference { get => Read("startReference", 0.0f); set => Write("startReference", value); }
	public float endReference { get => Read("endReference", 0.5f); set => Write("endReference", value); }
	public EasingType easingType { get => ReadEnum("easingType", EasingType.EaseOutSine); set => WriteEnum("easingType", value); }
}

public sealed partial class ParticleColorSettings {

	public EasingType easingType { get => ReadEnum("easingType", EasingType.EaseOutSine); set => WriteEnum("easingType", value); }
	public ParticleLoopSettings loop => new(Child("loop"));
	public bool useCurve { get => Read("useCurve", false); set => Write("useCurve", value); }
	public CurveChannelsSettings curve => new(values, "curveChannels", 4);
}

public sealed partial class ParticleEmissiveSettings : ParticleModuleSettings {

	internal ParticleEmissiveSettings(JsonObject values) : base(values) { }

	public bool useParticleColor { get => Read("useParticleColor", true); set => Write("useParticleColor", value); }
	public Color3 startColor { get => ReadColor3("startColor", new Color3(1, 1, 1)); set => WriteColor3("startColor", value); }
	public Color3 endColor { get => ReadColor3("endColor", new Color3(1, 1, 1)); set => WriteColor3("endColor", value); }
	public float startIntensity { get => Read("startIntensity", 1.0f); set => Write("startIntensity", value); }
	public float endIntensity { get => Read("endIntensity", 1.0f); set => Write("endIntensity", value); }
	public EasingType colorEasingType { get => ReadEnum("colorEasingType", EasingType.EaseOutSine); set => WriteEnum("colorEasingType", value); }
	public EasingType intensityEasingType { get => ReadEnum("intensityEasingType", EasingType.EaseOutSine); set => WriteEnum("intensityEasingType", value); }
}

public sealed partial class ParticleFlipbookSettings : ParticleModuleSettings {

	internal ParticleFlipbookSettings(JsonObject values) : base(values) { }

	public int[] tilesX { get => (values["tilesX"] as JsonArray ?? new JsonArray()).Select(node => node!.GetValue<int>()).ToArray(); set => values["tilesX"] = new JsonArray(value.Select(n => (JsonNode?)JsonValue.Create(n)).ToArray()); }
	public int tilesY { get => Read("tilesY", 1); set => Write("tilesY", value); }
	public float cycles { get => Read("cycles", 1.0f); set => Write("cycles", value); }
}
