using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	CurveSettings class
//	保存形式と同じキーと接線でCurveを編集する
//============================================================================
public sealed class CurveSettings : SettingsObject {

	internal CurveSettings(JsonObject values) : base(values) { }
	public string name { get => Read("name", "Value"); set => Write("name", value); }
	public float defaultValue { get => Read("defaultValue", 0f); set => Write("defaultValue", value); }
	public IReadOnlyList<CurveKeySettings> keys => (values["keys"] as JsonArray ?? new JsonArray())
		.OfType<JsonObject>().Select(key => new CurveKeySettings(key)).ToArray();
	public CurveKeySettings AddKey(float time, float value, CurveInterpolationMode interpolation = CurveInterpolationMode.Linear) {
		if (!float.IsFinite(time) || !float.IsFinite(value)) throw new ArgumentOutOfRangeException(nameof(time));
		if (values["keys"] is not JsonArray keys) values["keys"] = keys = new JsonArray();
		var key = new JsonObject { ["time"] = time, ["value"] = value, ["interpolation"] = interpolation.ToString() };
		keys.Add(key);
		return new(key);
	}
	public void RemoveKeyAt(int index) => ((JsonArray)values["keys"]!).RemoveAt(index);
	public void Clear() => values["keys"] = new JsonArray();
}

public sealed class CurveKeySettings : SettingsObject {

	internal CurveKeySettings(JsonObject values) : base(values) { }
	public float time { get => Read("time", 0f); set => Write("time", value); }
	public float value { get => Read("value", 0f); set => Write("value", value); }
	public CurveInterpolationMode interpolation { get => ReadEnum("interpolation", CurveInterpolationMode.Linear); set => WriteEnum("interpolation", value); }
	public Vector2 inTangent { get => ReadVector2("inTangent", Vector2.zero); set => WriteVector2("inTangent", value); }
	public Vector2 outTangent { get => ReadVector2("outTangent", Vector2.zero); set => WriteVector2("outTangent", value); }
}

public sealed class CurveChannelsSettings {

	private readonly JsonArray values;
	internal CurveChannelsSettings(JsonObject owner, string name, int count) {
		if (owner[name] is not JsonArray array) owner[name] = array = new JsonArray();
		values = array;
		while (values.Count < count) values.Add(new JsonObject());
	}
	public int count => values.Count;
	public CurveSettings this[int index] => new(values[index]!.AsObject());
}
