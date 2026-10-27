using System.IO;
using NEMEngine;

namespace SandboxScripts;

// 空Entityへ付け、Inspectorで確認対象を指定してPlayする
[ScriptTypeID("f6379ed6-3893-4af0-b52c-d3fbcf7fa8d1")]
public sealed class RuntimeAPIDemo : MonoBehaviour {

	[SerializeField, SerializedFieldID("8d07ee51-71d5-423b-96b0-26603376b73f"), Label("確認するCamera")]
	private PerspectiveCamera? targetCamera;
	[SerializeField, SerializedFieldID("6f664281-2365-45e8-b2d8-f00b719d4c77"), Label("確認するMeshRenderer")]
	private MeshRenderer? targetRenderer;
	[SerializeField, SerializedFieldID("84329e63-77f8-41fe-a84b-44345ed75682"), Label("確認するParticleSystem")]
	private ParticleSystem? targetParticles;
	[SerializeField, SerializedFieldID("20311aac-c51e-46dd-813d-9f6ea85a8624"), Label("確認するRenderTexture")]
	private RenderTexture? targetTexture;
	[SerializeField, SerializedFieldID("7073e4b5-6600-4095-995c-b374e49e3595"), Label("CSVファイル")]
	private TextAsset? csv;
	[SerializeField, SerializedFieldID("10088a5c-bbd3-450d-ae3d-44b873ec7766"), Label("JSONファイル")]
	private TextAsset? json;
	[SerializeField, SerializedFieldID("a67eb80c-ad4c-4a12-91b5-74c6b7582f0c"), Label("Pass名")]
	private string passName = "Bloom";
	private bool changed;
	private int fpsIndex;

	[Serializable]
	private sealed class SaveData {
		public int count = 1;
		public string label = "保存確認";
		public Vector3 position;
	}

	private void Start() {

		Debug.Log("Runtime API: F6=FPS、F7=RT影、F8=Camera/Material/Particle、F9=Pass、F10=Texture、F11=CSV/JSON、F12=設定保存");
		Debug.Log($"ユーザー保存先: {Application.persistentDataPath}");
	}

	private void Update() {

		try {
			if (Input.GetKeyDown(KeyCode.F6)) {
				Application.targetFrameRate = new[] { 30, 60, 120, -1 }[fpsIndex++ % 4];
				Debug.Log($"製品FPS上限: {Application.targetFrameRate} / Editor出力上限: {GraphicsSettings.GetEffectiveSettings().presentTargetFrameRate}");
			}
			if (Input.GetKeyDown(KeyCode.F7)) {
				var graphics = GraphicsSettings.GetSettings();
				graphics.rayTracingShadows = !graphics.rayTracingShadows;
				GraphicsSettings.ApplySettings(graphics);
				Debug.Log($"RT影の希望値: {graphics.rayTracingShadows}");
			}
			if (Input.GetKeyDown(KeyCode.F8)) ChangeComponents();
			if (Input.GetKeyDown(KeyCode.F9) && targetCamera) {
				var pass = targetCamera.renderPasses.FindPass(passName);
				if (!pass.isValid) Debug.Log($"Passがありません: {passName}");
				else { pass.enabled = !pass.enabled; Debug.Log($"{passName}: {pass.enabled}"); }
			}
			if (Input.GetKeyDown(KeyCode.F10) && targetTexture) {
				int size = targetTexture.width == 256 ? 512 : 256;
				targetTexture.Resize(size, size);
				Debug.Log($"RenderTexture: {targetTexture.width}×{targetTexture.height}");
			}
			if (Input.GetKeyDown(KeyCode.F11)) CheckDataFiles();
			if (Input.GetKeyDown(KeyCode.F12)) { GraphicsSettings.SaveUserSettings(); Debug.Log("ゲーム用ユーザー設定を保存しました"); }
		} catch (Exception error) { Debug.LogError($"Runtime API確認: {error.Message}"); }
	}

	// 設定値と描画結果の両方を確認する
	private void ChangeComponents() {

		changed = !changed;
		if (targetCamera) targetCamera.ColorFilter = changed ? new Color4(1, 0.5f, 0.5f, 1) : Color4.white;
		if (targetRenderer) targetRenderer.MaterialInstance.SetColor(MaterialParameterNames.BaseColor,
			changed ? new Color4(0.3f, 0.7f, 1, 1) : Color4.white);
		if (targetParticles) {
			var settings = targetParticles.GetSettings();
			var phase = settings.phases[0];
			var color = phase.GetColorOverLifetime() ?? phase.AddColorOverLifetime();
			color.startColor = changed ? new Color4(1, 0.2f, 0.1f, 1) : Color4.white;
			color.endColor = color.startColor;
			var emission = phase.GetEmissive() ?? phase.AddEmissive();
			emission.useParticleColor = true;
			emission.startIntensity = emission.endIntensity = changed ? 4 : 1;
			targetParticles.SetSettings(settings);
		}
		Debug.Log($"Component設定を変更: {changed}");
	}

	// 配布Assetとユーザー保存を別々に確認する
	private void CheckDataFiles() {

		if (csv) {
			var table = CsvUtility.Parse(csv.text, true);
			Debug.Log($"CSV: {table.rows.Count}行 / {table[0, "name"]}");
		}
		var data = json ? JsonUtility.FromJson<SaveData>(json.text) : new SaveData();
		data.count++;
		data.position = transform.position;
		Directory.CreateDirectory(Application.persistentDataPath);
		string path = Path.Combine(Application.persistentDataPath, "RuntimeAPIDemo.json");
		File.WriteAllText(path, JsonUtility.ToJson(data, true));
		var restored = JsonUtility.FromJson<SaveData>(File.ReadAllText(path));
		Debug.Log($"JSON往復: {restored.label}, count={restored.count}, 保存先={path}");
	}
}
