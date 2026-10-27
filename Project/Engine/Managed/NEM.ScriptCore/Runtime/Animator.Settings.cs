using System.Text.Json.Nodes;

namespace NEMEngine;

public enum AnimationWrapMode { UseClip, Once, Loop, PingPong }
public enum CurveInterpolationMode { Constant, Linear, Bezier, Spline, Squad }

//============================================================================
//	AnimationGroupSettings class
//	同時再生するClipの構成を編集する
//============================================================================
public sealed class AnimationGroupSettings : SettingsObject {

	public AnimationGroupSettings(string name) : base(new JsonObject { ["name"] = name, ["states"] = new JsonArray() }) { }
	internal AnimationGroupSettings(JsonObject values) : base(values) { }
	public string name { get => Read("name", ""); set => Write("name", value); }
	public IReadOnlyList<AnimationStateSettings> states => (values["states"] as JsonArray ?? new JsonArray())
		.OfType<JsonObject>().Select(value => new AnimationStateSettings(value)).ToArray();
	public AnimationStateSettings AddState(string name, AnimationClip clip) {
		ArgumentNullException.ThrowIfNull(clip);
		if (values["states"] is not JsonArray states) values["states"] = states = new JsonArray();
		var state = new JsonObject { ["name"] = name, ["clip"] = clip.assetID.ToString() };
		states.Add(state);
		return new(state);
	}
	public void RemoveStateAt(int index) => ((JsonArray)values["states"]!).RemoveAt(index);
}

public sealed class AnimationStateSettings : SettingsObject {

	internal AnimationStateSettings(JsonObject values) : base(values) { }
	public string name { get => Read("name", ""); set => Write("name", value); }
	public AnimationClip? clip { get { var id = ReadAsset("clip"); return id.isValid ? new AnimationClip(id) : null; } set => WriteAsset("clip", value?.assetID ?? AssetGUID.None); }
	public float speed { get => Read("speed", 1f); set => Write("speed", value); }
	public float weight { get => Read("weight", 1f); set => Write("weight", value); }
	public int priority { get => Read("priority", 0); set => Write("priority", value); }
	public bool additive { get => Read("additive", false); set => Write("additive", value); }
	public bool relativeTransform { get => Read("relativeTransform", false); set => Write("relativeTransform", value); }
	public AnimationWrapMode wrapMode { get => ReadEnum("wrapMode", AnimationWrapMode.UseClip); set => WriteEnum("wrapMode", value); }
	public int loopCount { get => Read("loopCount", 0); set => Write("loopCount", value); }
	public int pingPongCount { get => Read("pingPongCount", 0); set => Write("pingPongCount", value); }
	public float startDelay { get => Read("startDelay", 0f); set => Write("startDelay", value); }
	public float interval { get => Read("interval", 0f); set => Write("interval", value); }
	public bool loopBridgeEnabled { get => Child("loopBridge")["enabled"]?.GetValue<bool>() ?? false; set => Child("loopBridge")["enabled"] = value; }
	public float loopBridgeDuration { get => Child("loopBridge")["duration"]?.GetValue<float>() ?? 0.1f; set => Child("loopBridge")["duration"] = value; }
	public CurveInterpolationMode loopBridgeInterpolation {
		get => Enum.TryParse<CurveInterpolationMode>(Child("loopBridge")["interpolation"]?.GetValue<string>(), out var mode) ? mode : CurveInterpolationMode.Linear;
		set => Child("loopBridge")["interpolation"] = value.ToString();
	}
}

public sealed partial class Animator {

	public IReadOnlyList<AnimationGroupSettings> GetGroups() =>
		(ComponentSettingsTransfer.Read(native, componentTypeID)["groups"] as JsonArray ?? new JsonArray())
		.OfType<JsonObject>().Select(value => new AnimationGroupSettings(value)).ToArray();
	public void SetGroups(IEnumerable<AnimationGroupSettings> groups) {
		ArgumentNullException.ThrowIfNull(groups);
		var array = new JsonArray(groups.Select(group => (JsonNode?)group.values.DeepClone()).ToArray());
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject { ["groups"] = array });
	}
}
