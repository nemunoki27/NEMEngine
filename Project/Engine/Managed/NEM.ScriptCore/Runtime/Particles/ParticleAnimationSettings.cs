using System.Text.Json;
using System.Text.Json.Nodes;

namespace NEMEngine;

public enum ParticleValueMode { Constant, Random }
public enum ParticleLoopType { Repeat, PingPong }
public enum ParticleLifeEndMode { Advance, Clamp, Reset, Kill }
public enum ParticleTrailMode { Particle, Ribbon }
public enum ParticleTrailTextureMode { Stretch, Tile }
[Flags] public enum ParticleAxes { None = 0, X = 1, Y = 2, Z = 4, All = 7 }

//============================================================================
//	ParticleValueSettings class
//	定数とランダム範囲を編集する
//============================================================================
public sealed class ParticleValueSettings<T> : SettingsObject where T : struct {

	private static readonly JsonSerializerOptions options = new() { IncludeFields = true, IgnoreReadOnlyProperties = true };
	internal ParticleValueSettings(JsonObject values) : base(values) { }
	public ParticleValueMode mode { get => ReadEnum("type", ParticleValueMode.Constant); set => WriteEnum("type", value); }
	public T constant { get => ReadValue("constant"); set => values["constant"] = JsonSerializer.SerializeToNode(value, options); }
	public T minimum { get => ReadValue("min"); set => values["min"] = JsonSerializer.SerializeToNode(value, options); }
	public T maximum { get => ReadValue("max"); set => values["max"] = JsonSerializer.SerializeToNode(value, options); }
	public void SetConstant(T value) { mode = ParticleValueMode.Constant; constant = minimum = maximum = value; }
	public void SetRange(T minimum, T maximum) { mode = ParticleValueMode.Random; this.minimum = minimum; this.maximum = maximum; }
	private T ReadValue(string name) => values[name] is JsonNode value ? value.Deserialize<T>(options) : default;
}

public sealed class ParticleLoopSettings : SettingsObject {

	internal ParticleLoopSettings(JsonObject values) : base(values) { }
	public int loopCount { get => Read("loopCount", 1); set => Write("loopCount", value); }
	public ParticleLoopType type { get => ReadEnum("type", ParticleLoopType.Repeat); set => WriteEnum("type", value); }
}

public class ParticleFloatAnimationSettings : SettingsObject {

	internal ParticleFloatAnimationSettings(JsonObject values) : base(values) { }
	public float start { get => Read("start", 0f); set => Write("start", value); }
	public float end { get => Read("end", 1f); set => Write("end", value); }
	public EasingType easingType { get => ReadEnum("easingType", EasingType.EaseOutSine); set => WriteEnum("easingType", value); }
	public ParticleLoopSettings loop => new(Child("loop"));
	public bool useCurve { get => Read("useCurve", false); set => Write("useCurve", value); }
	public CurveSettings curve => new(Child("curve"));
}

public class ParticleVector2AnimationSettings : SettingsObject {

	internal ParticleVector2AnimationSettings(JsonObject values) : base(values) { }
	public Vector2 start { get => ReadVector2("start", Vector2.zero); set => WriteVector2("start", value); }
	public Vector2 end { get => ReadVector2("end", Vector2.zero); set => WriteVector2("end", value); }
	public EasingType easingType { get => ReadEnum("easingType", EasingType.EaseOutSine); set => WriteEnum("easingType", value); }
	public ParticleLoopSettings loop => new(Child("loop"));
	public bool useCurve { get => Read("useCurve", false); set => Write("useCurve", value); }
	public CurveChannelsSettings curveChannels => new(values, "curveChannels", 2);
}

//============================================================================
//	ParticleModuleSettings class
//	Moduleの設定と有効状態を編集する
//============================================================================
public abstract class ParticleModuleSettings : ParticleSettingsObject {

	internal ParticleModuleSettings(JsonObject values) : base(values) { }
	public string moduleID => (values.Parent as JsonObject)?["id"]?.GetValue<string>() ?? "";
	public UUID instanceID => UUID.Parse((values.Parent as JsonObject)?["instanceID"]?.GetValue<string>());
	public bool moduleEnabled {
		get => (values.Parent as JsonObject)?["enabled"]?.GetValue<bool>() ?? true;
		set {
			if (values.Parent is not JsonObject entry) throw new InvalidOperationException("ModuleがPhaseに所属していません");
			entry["enabled"] = value;
		}
	}
}

public sealed class ParticlePhaseParentSettings : SettingsObject {

	internal ParticlePhaseParentSettings(JsonObject values) : base(values) { }
	public bool useEmitter { get => Read("useEmitter", false); set => Write("useEmitter", value); }
	public bool ignoreParentRotation { get => Read("ignoreParentRotation", false); set => Write("ignoreParentRotation", value); }
	public bool ignoreParentScale { get => Read("ignoreParentScale", true); set => Write("ignoreParentScale", value); }
	public bool keepWorldOnDetach { get => Read("keepWorldOnDetach", true); set => Write("keepWorldOnDetach", value); }
}
