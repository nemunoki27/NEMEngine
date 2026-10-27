using System.Text.Json.Nodes;

namespace NEMEngine;

public enum ParticleEmitterShape { Sphere, Hemisphere, Box, Torus, Circle, Cone, Point, Rect, Cone2D }

public sealed partial class ParticleEmitterSettings : ParticleSettingsObject {

	internal ParticleEmitterSettings(JsonObject values) : base(values) { }
	public ParticleEmitterShape shape { get => ReadEnum("shape", ParticleEmitterShape.Sphere); set => WriteEnum("shape", value); }
	public float emitInterval { get => Read("emitInterval", 0.1f); set => Write("emitInterval", value); }
	public uint maxParticles { get => Read("maxParticles", 1024u); set => Write("maxParticles", value); }
	public uint emitCount {
		get => ReadConstant("emitCount", 4u);
		set => WriteConstant("emitCount", value);
	}
	public float speed {
		get => ReadConstant("speed", 1.6f);
		set => WriteConstant("speed", value);
	}
	public Vector3 pointDirection { get => ReadVector3("pointDirection", Vector3.up); set => WriteVector3("pointDirection", value); }

	public void SetOffsetRange(Vector3 minimum, Vector3 maximum) {
		JsonObject offset = Child("emitOffset");
		offset["type"] = "Random";
		offset["min"] = Pack(minimum);
		offset["max"] = Pack(maximum);
	}

	public void SetOffset(Vector3 offset) => values["emitOffset"] = new JsonObject {
		["type"] = "Constant", ["constant"] = Pack(offset), ["min"] = Pack(offset), ["max"] = Pack(offset),
	};

	private T ReadConstant<T>(string name, T fallback) => (values[name] as JsonObject)?["constant"] is JsonNode value
		? value.GetValue<T>() : fallback;
	private void WriteConstant<T>(string name, T value) => values[name] = new JsonObject {
		["type"] = "Constant", ["constant"] = JsonValue.Create(value), ["min"] = JsonValue.Create(value), ["max"] = JsonValue.Create(value),
	};
	private static JsonObject Pack(Vector3 value) => new() { ["x"] = value.x, ["y"] = value.y, ["z"] = value.z };
}
