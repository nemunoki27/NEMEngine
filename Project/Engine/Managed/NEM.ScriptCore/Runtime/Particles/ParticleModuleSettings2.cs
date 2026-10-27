using System.Text.Json.Nodes;

namespace NEMEngine;

// Moduleの編集値を実行用Snapshotへ保持する
public sealed partial class ParticleGravitySettings {

	public bool reflectGround { get => Read("reflectGround", false); set => Write("reflectGround", value); }
	public bool autoGroundEmitter { get => Read("autoGroundEmitter", false); set => Write("autoGroundEmitter", value); }
	public float reflectGroundY { get => Read("reflectGroundY", 0.0f); set => Write("reflectGroundY", value); }
	public float restitution { get => Read("restitution", 0.4f); set => Write("restitution", value); }
}

public sealed partial class ParticleLookToVelocitySettings : ParticleModuleSettings {

	internal ParticleLookToVelocitySettings(JsonObject values) : base(values) { }


}

public sealed partial class ParticleNoiseForceSettings : ParticleModuleSettings {

	internal ParticleNoiseForceSettings(JsonObject values) : base(values) { }

	public float strength { get => Read("strength", 1.0f); set => Write("strength", value); }
	public float frequency { get => Read("frequency", 1.0f); set => Write("frequency", value); }
}

public sealed partial class ParticleNoiseUVSettings : ParticleModuleSettings {

	internal ParticleNoiseUVSettings(JsonObject values) : base(values) { }

	public float strength { get => Read("strength", 0.1f); set => Write("strength", value); }
	public float frequency { get => Read("frequency", 1.0f); set => Write("frequency", value); }
}

public sealed partial class ParticlePendulumSettings : ParticleModuleSettings {

	internal ParticlePendulumSettings(JsonObject values) : base(values) { }

	public ParticleFloatAnimationSettings length => new(Child("length"));
	public ParticleFloatAnimationSettings maxAngle => new(Child("maxAngle"));
	public ParticleFloatAnimationSettings cycles => new(Child("cycles"));
	public float planeAngle { get => Read("planeAngle", 0.0f); set => Write("planeAngle", value); }
	public float startPhase { get => Read("startPhase", 0.0f); set => Write("startPhase", value); }
	public float particlePhaseOffset { get => Read("particlePhaseOffset", 0.0f); set => Write("particlePhaseOffset", value); }
	public float arcStrength { get => Read("arcStrength", 1.0f); set => Write("arcStrength", value); }
	public bool reverse { get => Read("reverse", false); set => Write("reverse", value); }
}
