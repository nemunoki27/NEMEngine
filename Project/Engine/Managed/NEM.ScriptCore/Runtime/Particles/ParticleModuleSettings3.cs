using System.Text.Json.Nodes;

namespace NEMEngine;

// Moduleの編集値を実行用Snapshotへ保持する
public sealed partial class ParticleRotationSettings : ParticleModuleSettings {

	internal ParticleRotationSettings(JsonObject values) : base(values) { }

	public ParticleRotationMode mode { get => ReadEnum("mode", ParticleRotationMode.Fixed); set => WriteEnum("mode", value); }
	public ParticleRotationValueType valueType { get => ReadEnum("valueType", ParticleRotationValueType.Euler); set => WriteEnum("valueType", value); }
	public ParticleValueSettings<Vector3> fixedAngle => new(Child("fixedAngle"));
	public Vector3 fixedAxis { get => ReadVector3("fixedAxis", Vector3.zero); set => WriteVector3("fixedAxis", value); }
	public ParticleValueSettings<float> fixedQuaternionAngle => new(Child("fixedQuaternionAngle"));
	public ParticleValueSettings<Vector3> addAngle => new(Child("addAngle"));
	public Vector3 addAxis { get => ReadVector3("addAxis", Vector3.zero); set => WriteVector3("addAxis", value); }
	public ParticleValueSettings<float> addQuaternionAngle => new(Child("addQuaternionAngle"));
	public ParticleRotationSpeedMode speedMode { get => ReadEnum("speedMode", ParticleRotationSpeedMode.Constant); set => WriteEnum("speedMode", value); }
	public ParticleValueSettings<Vector3> rotationSpeed => new(Child("rotationSpeed"));
	public Vector3 rotationSpeedAxis { get => ReadVector3("rotationSpeedAxis", Vector3.zero); set => WriteVector3("rotationSpeedAxis", value); }
	public ParticleValueSettings<float> rotationQuaternionSpeed => new(Child("rotationQuaternionSpeed"));
	public Vector3 startSpeed { get => ReadVector3("startSpeed", Vector3.zero); set => WriteVector3("startSpeed", value); }
	public Vector3 endSpeed { get => ReadVector3("endSpeed", Vector3.zero); set => WriteVector3("endSpeed", value); }
	public Vector3 startSpeedAxis { get => ReadVector3("startSpeedAxis", Vector3.zero); set => WriteVector3("startSpeedAxis", value); }
	public Vector3 endSpeedAxis { get => ReadVector3("endSpeedAxis", Vector3.zero); set => WriteVector3("endSpeedAxis", value); }
	public float startQuaternionSpeed { get => Read("startQuaternionSpeed", 0.0f); set => Write("startQuaternionSpeed", value); }
	public float endQuaternionSpeed { get => Read("endQuaternionSpeed", 0.0f); set => Write("endQuaternionSpeed", value); }
	public EasingType speedEasingType { get => ReadEnum("speedEasingType", EasingType.EaseOutSine); set => WriteEnum("speedEasingType", value); }
	public ParticleLoopSettings speedLoop => new(Child("speedLoop"));
	public bool useSpeedCurve { get => Read("useSpeedCurve", false); set => Write("useSpeedCurve", value); }
	public CurveChannelsSettings speedCurve => new(values, "speedCurveChannels", 3);
	public QuaternionCurveSettings speedQuaternionCurve => new(values, "speedQuaternionCurveChannels", "speedQuaternionCurveAxisKeys");
	public Vector3 startAngle { get => ReadVector3("startAngle", Vector3.zero); set => WriteVector3("startAngle", value); }
	public Vector3 endAngle { get => ReadVector3("endAngle", Vector3.zero); set => WriteVector3("endAngle", value); }
	public Vector3 startAxis { get => ReadVector3("startAxis", Vector3.zero); set => WriteVector3("startAxis", value); }
	public Vector3 endAxis { get => ReadVector3("endAxis", Vector3.zero); set => WriteVector3("endAxis", value); }
	public float startQuaternionAngle { get => Read("startQuaternionAngle", 0.0f); set => Write("startQuaternionAngle", value); }
	public float endQuaternionAngle { get => Read("endQuaternionAngle", 0.0f); set => Write("endQuaternionAngle", value); }
	public EasingType angleEasingType { get => ReadEnum("angleEasingType", EasingType.EaseOutSine); set => WriteEnum("angleEasingType", value); }
	public ParticleLoopSettings angleLoop => new(Child("angleLoop"));
	public bool useAngleCurve { get => Read("useAngleCurve", false); set => Write("useAngleCurve", value); }
	public CurveChannelsSettings angleCurve => new(values, "angleCurveChannels", 3);
	public QuaternionCurveSettings quaternionCurve => new(values, "quaternionCurveChannels", "quaternionCurveAxisKeys");
}

public sealed partial class ParticleScaleSettings {

	public EasingType easingType { get => ReadEnum("easingType", EasingType.EaseOutSine); set => WriteEnum("easingType", value); }
	public ParticleLoopSettings loop => new(Child("loop"));
	public bool useCurve { get => Read("useCurve", false); set => Write("useCurve", value); }
	public CurveChannelsSettings curve => new(values, "curveChannels", 3);
}

public sealed partial class ParticleSizeSettings {

	public EasingType easingType { get => ReadEnum("easingType", EasingType.EaseOutSine); set => WriteEnum("easingType", value); }
	public ParticleLoopSettings loop => new(Child("loop"));
	public bool useCurve { get => Read("useCurve", false); set => Write("useCurve", value); }
	public CurveSettings curve => new(Child("curve"));
}

public sealed partial class ParticleSpiralSettings : ParticleModuleSettings {

	internal ParticleSpiralSettings(JsonObject values) : base(values) { }

	public ParticleFloatAnimationSettings radius => new(Child("radius"));
	public ParticleFloatAnimationSettings turns => new(Child("turns"));
	public float startAngle { get => Read("startAngle", 0.0f); set => Write("startAngle", value); }
	public float particleAngleOffset { get => Read("particleAngleOffset", 0.0f); set => Write("particleAngleOffset", value); }
	public bool reverse { get => Read("reverse", false); set => Write("reverse", value); }
}
