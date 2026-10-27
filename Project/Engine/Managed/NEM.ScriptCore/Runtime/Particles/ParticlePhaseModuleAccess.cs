using System.Text.Json.Nodes;

namespace NEMEngine;

public sealed partial class ParticlePhaseSettings {

	public ParticleAlphaReferenceSettings AddAlphaReference() => new(AddModule("AlphaReference"));
	public ParticleAlphaReferenceSettings? GetAlphaReference(int occurrence = 0) => FindModule("AlphaReference", occurrence) is JsonObject data ? new(data) : null;
	public ParticleColorSettings? GetColorOverLifetime(int occurrence = 0) => FindModule("ColorOverLifetime", occurrence) is JsonObject data ? new(data) : null;
	public ParticleCurlNoiseSettings? GetCurlNoise(int occurrence = 0) => FindModule("CurlNoise", occurrence) is JsonObject data ? new(data) : null;
	public ParticleEmissiveSettings AddEmissive() => new(AddModule("Emissive"));
	public ParticleEmissiveSettings? GetEmissive(int occurrence = 0) => FindModule("Emissive", occurrence) is JsonObject data ? new(data) : null;
	public ParticleFlipbookSettings AddFlipbook() => new(AddModule("Flipbook"));
	public ParticleFlipbookSettings? GetFlipbook(int occurrence = 0) => FindModule("Flipbook", occurrence) is JsonObject data ? new(data) : null;
	public ParticleGravitySettings? GetGravity(int occurrence = 0) => FindModule("GravityForce", occurrence) is JsonObject data ? new(data) : null;
	public ParticleLookToVelocitySettings AddLookToVelocity() => new(AddModule("LookToVelocity"));
	public ParticleLookToVelocitySettings? GetLookToVelocity(int occurrence = 0) => FindModule("LookToVelocity", occurrence) is JsonObject data ? new(data) : null;
	public ParticleNoiseForceSettings AddNoiseForce() => new(AddModule("NoiseForce"));
	public ParticleNoiseForceSettings? GetNoiseForce(int occurrence = 0) => FindModule("NoiseForce", occurrence) is JsonObject data ? new(data) : null;
	public ParticleNoiseUVSettings AddNoiseUV() => new(AddModule("NoiseUV"));
	public ParticleNoiseUVSettings? GetNoiseUV(int occurrence = 0) => FindModule("NoiseUV", occurrence) is JsonObject data ? new(data) : null;
	public ParticlePendulumSettings AddPendulumMovement() => new(AddModule("PendulumMovement"));
	public ParticlePendulumSettings? GetPendulumMovement(int occurrence = 0) => FindModule("PendulumMovement", occurrence) is JsonObject data ? new(data) : null;
	public ParticleRotationSettings AddRotation() => new(AddModule("Rotation"));
	public ParticleRotationSettings? GetRotation(int occurrence = 0) => FindModule("Rotation", occurrence) is JsonObject data ? new(data) : null;
	public ParticleScaleSettings? GetScaleOverLifetime(int occurrence = 0) => FindModule("ScaleOverLifetime", occurrence) is JsonObject data ? new(data) : null;
	public ParticleSizeSettings? GetSizeOverLifetime(int occurrence = 0) => FindModule("SizeOverLifetime", occurrence) is JsonObject data ? new(data) : null;
	public ParticleSpiralSettings AddSpiralMovement() => new(AddModule("SpiralMovement"));
	public ParticleSpiralSettings? GetSpiralMovement(int occurrence = 0) => FindModule("SpiralMovement", occurrence) is JsonObject data ? new(data) : null;
	public ParticleUVTransformSettings AddColorUV() => new(AddModule("ColorUV"));
	public ParticleUVTransformSettings? GetColorUV(int occurrence = 0) => FindModule("ColorUV", occurrence) is JsonObject data ? new(data) : null;
	public ParticleShaderParametersSettings AddCustomShaderParameter() => new(AddModule("CustomShaderParameter"));
	public ParticleShaderParametersSettings? GetCustomShaderParameter(int occurrence = 0) => FindModule("CustomShaderParameter", occurrence) is JsonObject data ? new(data) : null;
	public ParticleShapeSettings AddShapeOverLifetime() => new(AddModule("ShapeOverLifetime"));
	public ParticleShapeSettings? GetShapeOverLifetime(int occurrence = 0) => FindModule("ShapeOverLifetime", occurrence) is JsonObject data ? new(data) : null;
}
