using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed class ParticleCurlNoiseSettings : ParticleSettingsObject {

	internal ParticleCurlNoiseSettings(JsonObject values) : base(values) { }
	public float strength { get => Read("strength", 1f); set => Write("strength", value); }
	public float frequency { get => Read("frequency", 1f); set => Write("frequency", value); }
	public Vector3 scrollSpeed { get => ReadVector3("scrollSpeed", Vector3.zero); set => WriteVector3("scrollSpeed", value); }
}

public sealed class ParticleGravitySettings : ParticleSettingsObject {

	internal ParticleGravitySettings(JsonObject values) : base(values) { }
	public Vector3 gravity { get => ReadVector3("gravity", new(0, -9.8f, 0)); set => WriteVector3("gravity", value); }
}

public sealed class ParticleSizeSettings : ParticleSettingsObject {

	internal ParticleSizeSettings(JsonObject values) : base(values) { }
	public float startScale { get => Read("startScale", 1f); set => Write("startScale", value); }
	public float endScale { get => Read("endScale", 0f); set => Write("endScale", value); }
}

public sealed class ParticleColorSettings : ParticleSettingsObject {

	internal ParticleColorSettings(JsonObject values) : base(values) { }
	public Color4 startColor { get => ReadColor("startColor", Color4.white); set => WriteColor("startColor", value); }
	public Color4 endColor { get => ReadColor("endColor", new(1, 1, 1, 0)); set => WriteColor("endColor", value); }
}

public sealed class ParticleScaleSettings : ParticleSettingsObject {

	internal ParticleScaleSettings(JsonObject values) : base(values) { }
	public Vector3 startScale { get => ReadVector3("startScale", Vector3.one); set => WriteVector3("startScale", value); }
	public Vector3 endScale { get => ReadVector3("endScale", Vector3.one); set => WriteVector3("endScale", value); }
}

public enum ParticleParameterMode { Constant, OverLifetime }

public sealed class ParticleVector4Animation : ParticleSettingsObject {

	internal ParticleVector4Animation(JsonObject values) : base(values) { }
	public ParticleParameterMode mode { get => ReadEnum("mode", ParticleParameterMode.Constant); set => WriteEnum("mode", value); }
	public Vector4 constant { get => ReadVector4("constant", Vector4.zero); set => WriteVector4("constant", value); }
	public Vector4 start { get => ReadVector4("start", Vector4.zero); set => WriteVector4("start", value); }
	public Vector4 end { get => ReadVector4("end", Vector4.zero); set => WriteVector4("end", value); }
}

public sealed class ParticleCustomDataSettings : ParticleSettingsObject {

	internal ParticleCustomDataSettings(JsonObject values) : base(values) { }
	public ParticleVector4Animation customData1 => new(Child("customData1"));
	public ParticleVector4Animation customData2 => new(Child("customData2"));
}
