using System.Text.Json.Nodes;

namespace NEMEngine;

public enum ParticleRotationMode { Fixed, Additive, Interpolate }
public enum ParticleRotationSpeedMode { Constant, OverLifetime }
public enum ParticleRotationValueType { Euler, Quaternion }
