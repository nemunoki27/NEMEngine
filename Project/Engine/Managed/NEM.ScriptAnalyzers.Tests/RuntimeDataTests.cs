using System.Text.Json;
using System.Text.Json.Nodes;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

//============================================================================
//	RuntimeDataTests class
//	CSVとJSONの往復と入力失敗時の保持を確認する
//============================================================================
internal static class RuntimeDataTests {

	[Serializable]
	private sealed class SaveData {
		public int count = 1;
		[SerializeField] private string label = "初期値";
		[NonSerialized] public int transient = 7;
		public Vector3 position;
		public List<int> values = new();
		public string Label => label;
		public NestedData nested = new();
	}

	[Serializable]
	private sealed class NestedData { public int first = 10; public int second = 20; }

	internal static void Run() {
		var csv = CsvUtility.Parse("\uFEFFname,value,note\r\n日本語,12,\"A,B\r\nC\"\r\n\"a\"\"b\",,\r\n", true);
		Check(csv.rows.Count == 2 && csv[0, "name"] == "日本語" && csv[0, 2] == "A,B\r\nC");
		Check(csv[1, 0] == "a\"b" && csv[1, 1] == "" && csv[1, 2] == "");
		Check(csv.TryGetInt32(0, 1, out int number) && number == 12);
		var copy = CsvUtility.Parse(CsvUtility.Write(csv), true);
		Check(copy[0, 2] == csv[0, 2] && copy.rows.Count == 2);
		Check(CsvUtility.Parse("").rows.Count == 0 && CsvUtility.Parse("a,").rows[0].Count == 2);
		bool rejected = false;
		try { CsvUtility.Parse("a,\"unfinished"); } catch (FormatException) { rejected = true; }
		Check(rejected);

		var data = new SaveData { count = 42, position = new(1, 2, 3), values = [2, 4, 8] };
		string json = JsonUtility.ToJson(data);
		Check(!json.Contains("transient") && !json.Contains("Label") && json.Contains("label"));
		var restored = JsonUtility.FromJson<SaveData>(json);
		Check(restored.count == 42 && restored.Label == "初期値" && restored.position.y == 2 && restored.values.Count == 3);
		JsonUtility.FromJsonOverwrite("{\"count\":5}", restored);
		Check(restored.count == 5 && restored.values.Count == 3);
		JsonUtility.FromJsonOverwrite("{\"nested\":{\"first\":99}}", restored);
		Check(restored.nested.first == 99 && restored.nested.second == 20);
		try { JsonUtility.FromJsonOverwrite("{\"count\":8,\"values\":\"invalid\"}", restored); }
		catch (JsonException) { }
		Check(restored.count == 5 && restored.values.Count == 3);
		CheckParticleSettings();
		CheckCameraSettings();
		var graphics = new GraphicsSettingsData(new JsonObject { ["allowMeshShader"] = true,
			["targetFrameRate"] = 60, ["width"] = 1280, ["playerGamepads"] = new JsonArray(0, 1, 2, 3) });
		Check(graphics.useMeshShader && !graphics.values.ContainsKey("targetFrameRate") &&
			!graphics.values.ContainsKey("width") && !graphics.values.ContainsKey("playerGamepads"));
	}

	// Module本体と有効フラグ、Curveとランダム値を別々に保持する
	private static void CheckParticleSettings() {
		var settings = new ParticleSystemSettings(new JsonObject { ["version"] = 1, ["phases"] = new JsonArray() });
		var phase = settings.AddPhase("確認");
		var color = phase.AddColorOverLifetime();
		color.startColor = new Color4(0.2f, 0.4f, 0.6f, 0.8f);
		color.moduleEnabled = false;
		color.useCurve = true;
		color.curve[0].AddKey(0, 0.2f);
		color.curve[0].AddKey(1, 0.8f);
		var emission = phase.AddEmissive();
		emission.useParticleColor = true;
		emission.startIntensity = 4;
		settings.emitter.startSpeed.SetRange(1, 4);
		phase.lifetimeSettings.SetRange(2, 3);
		Check(phase.moduleCount == 2 && !phase.GetModuleEnabled(0) && phase.GetModuleEnabled(1));
		phase.MoveModule(1, 0);
		var roundTrip = new ParticleSystemSettings(JsonNode.Parse(settings.values.ToJsonString())!.AsObject());
		Check(roundTrip.phases[0].GetEmissive()!.startIntensity == 4);
		Check(!roundTrip.phases[0].GetColorOverLifetime()!.moduleEnabled);
		Check(roundTrip.phases[0].GetColorOverLifetime()!.curve[0].keys.Count == 2);
		Check(roundTrip.emitter.startSpeed.mode == ParticleValueMode.Random && roundTrip.emitter.startSpeed.maximum == 4);
		Check(roundTrip.phases[0].lifetimeSettings.minimum == 2);
	}

	// 同じSnapshotを使う別Cameraへ変更が漏れない
	private static void CheckCameraSettings() {
		var data = JsonNode.Parse("{\"renderPasses\":{\"passes\":[{\"id\":\"123456789abcdef0\",\"name\":\"Bloom\",\"outputs\":[{\"name\":\"Color\",\"widthScale\":1}]}]}}")!.AsObject();
		var first = new CameraRenderingSettings((JsonObject)data.DeepClone());
		var second = new CameraRenderingSettings((JsonObject)data.DeepClone());
		first.renderPasses!.FindPass("Bloom")!.outputs[0].widthScale = 0.5f;
		Check(second.renderPasses!.FindPass("Bloom")!.outputs[0].widthScale == 1);
		var exported = first.renderPasses.ToJson();
		exported["passes"]![0]!["enabled"] = false;
		Check(first.renderPasses.FindPass("Bloom")!.enabled);
		first.renderPasses.Import(exported);
		Check(!first.renderPasses.FindPass("Bloom")!.enabled);
	}

	// データの不一致はテストを失敗させる
	private static void Check(bool value) {
		if (!value) throw new InvalidOperationException("Runtime data contract failed");
	}
}
