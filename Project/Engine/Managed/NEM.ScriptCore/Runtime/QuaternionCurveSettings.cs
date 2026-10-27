using System.Text.Json.Nodes;

namespace NEMEngine;

//============================================================================
//	QuaternionCurveSettings class
//	回転軸と回転角度のCurveを編集する
//============================================================================
public sealed class QuaternionCurveSettings {

	private readonly JsonObject owner;
	private readonly string channelName;
	private readonly string axesName;
	internal QuaternionCurveSettings(JsonObject owner, string channelName, string axesName) {
		this.owner = owner; this.channelName = channelName; this.axesName = axesName;
	}
	public CurveChannelsSettings channels => new(owner, channelName, 2);
	public IReadOnlyList<QuaternionCurveAxisSettings> axisKeys => (owner[axesName] as JsonArray ?? new JsonArray())
		.OfType<JsonObject>().Select(value => new QuaternionCurveAxisSettings(value)).ToArray();
	public QuaternionCurveAxisSettings AddAxisKey() {
		if (owner[axesName] is not JsonArray keys) owner[axesName] = keys = new JsonArray();
		var key = new JsonObject { ["axes"] = new JsonArray("X") };
		keys.Add(key);
		return new(key);
	}
	public void RemoveAxisKeyAt(int index) => ((JsonArray)owner[axesName]!).RemoveAt(index);
}

public sealed class QuaternionCurveAxisSettings : SettingsObject {

	internal QuaternionCurveAxisSettings(JsonObject values) : base(values) { }
	public bool useCustomAxis { get => Read("useCustomAxis", false); set => Write("useCustomAxis", value); }
	public Vector3 customAxis { get => ReadVector3("customAxis", new(1, 0, 0)); set => WriteVector3("customAxis", value); }
	public ParticleAxes axes {
		get => (values["axes"] as JsonArray ?? new JsonArray()).Aggregate(ParticleAxes.None,
			(result, item) => result | (Enum.TryParse<ParticleAxes>(item?.GetValue<string>(), out var axis) ? axis : ParticleAxes.None));
		set => values["axes"] = new JsonArray(new[] { ParticleAxes.X, ParticleAxes.Y, ParticleAxes.Z }
			.Where(axis => (value & axis) != 0).Select(axis => (JsonNode?)JsonValue.Create(axis.ToString())).ToArray());
	}
}
