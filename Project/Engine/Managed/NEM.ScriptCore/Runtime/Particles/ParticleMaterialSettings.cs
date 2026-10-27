using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	ParticleMaterialSettings class
//	Textureと名前付きパラメータを編集する
//============================================================================
public sealed class ParticleMaterialSettings : SettingsObject {

	internal ParticleMaterialSettings(JsonObject values) : base(values) { }
	public AssetGUID baseColorTexture { get => ReadAsset("baseColorTexture"); set => WriteAsset("baseColorTexture", value); }
	public IReadOnlyDictionary<string, AssetGUID> textures => Child("textureOverrides")
		.ToDictionary(item => item.Key, item => AssetGUID.Parse(item.Value?.GetValue<string>()));
	public void SetTexture(string name, AssetGUID texture) {
		ArgumentException.ThrowIfNullOrEmpty(name);
		Child("textureOverrides")[name] = texture.isValid ? texture.ToString() : "";
	}
	public bool RemoveTexture(string name) => Child("textureOverrides").Remove(name);
	public ParticleVector4Animation GetParameter(string name) {
		ArgumentException.ThrowIfNullOrEmpty(name);
		var parameters = Child("parameters");
		if (parameters[name] is not JsonObject parameter) parameters[name] = parameter = new JsonObject();
		return new(parameter);
	}
	public bool RemoveParameter(string name) => Child("parameters").Remove(name);
}

public sealed class ParticleShaderParametersSettings : ParticleModuleSettings {

	internal ParticleShaderParametersSettings(JsonObject values) : base(values) { }
	public ParticleVector4Animation GetParameter(string name) => new ParticleMaterialSettings(values).GetParameter(name);
	public bool RemoveParameter(string name) => Child("parameters").Remove(name);
}

public sealed class ParticleShapeSettings : ParticleModuleSettings {

	internal ParticleShapeSettings(JsonObject values) : base(values) { }
	public PrimitiveType shape { get => ReadEnum("shape", PrimitiveType.Ring); set => WriteEnum("shape", value); }
	public ParticleVector4Animation GetParameter(string name) => new ParticleMaterialSettings(values).GetParameter(name);
	public bool RemoveParameter(string name) => Child("parameters").Remove(name);
}

public sealed partial class ParticleVector4Animation {

	public EasingType easingType { get => ReadEnum("easingType", EasingType.EaseOutSine); set => WriteEnum("easingType", value); }
	public ParticleLoopSettings loop => new(Child("loop"));
	public bool useCurve { get => Read("useCurve", false); set => Write("useCurve", value); }
	public uint componentCount { get => Read("componentCount", 1u); set => Write("componentCount", value); }
	public CurveChannelsSettings curveRGB => new(values, "curve3", 3);
	public CurveSettings curveAlpha => new(Child("curveW"));
}

public enum ParticleUVUpdateType { Lerp, Scroll }

public sealed class ParticleUVTransformSettings : ParticleModuleSettings {

	internal ParticleUVTransformSettings(JsonObject values) : base(values) { }
	public ParticleUVOffsetSettings offset => new(Child("offset"));
	public ParticleVector2AnimationSettings scale => new(Child("scale"));
	public ParticleUVRotationSettings rotation => new(Child("rotation"));
}

public sealed class ParticleUVOffsetSettings : ParticleVector2AnimationSettings {

	internal ParticleUVOffsetSettings(JsonObject values) : base(values) { }
	public ParticleUVUpdateType updateType { get => ReadEnum("updateType", ParticleUVUpdateType.Lerp); set => WriteEnum("updateType", value); }
	public Vector2 scrollSpeed { get => ReadVector2("scrollSpeed", Vector2.zero); set => WriteVector2("scrollSpeed", value); }
}

public sealed class ParticleUVRotationSettings : ParticleFloatAnimationSettings {

	internal ParticleUVRotationSettings(JsonObject values) : base(values) { }
	public Vector2 pivot { get => ReadVector2("pivot", new(0.5f, 0.5f)); set => WriteVector2("pivot", value); }
}
