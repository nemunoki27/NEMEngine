using System.Text.Json.Nodes;

namespace NEMEngine;

public enum RenderPassKind { Compute, RayTracing, Bloom }
public enum RenderPassAnchor { BeforeLighting, AfterLighting, BeforeTransparent, AfterTransparent, AfterPostProcessUI, BeforeBlit, AfterToneMap }
public enum RenderPassSourceKind { PreviousPass, SceneColor, PassOutput }
public enum RenderPassOutputFormat { Inherit, R8_UNORM, R16_FLOAT, RG16_FLOAT, RGBA16_FLOAT, R32_FLOAT, RG32_FLOAT, RGBA32_FLOAT }

//============================================================================
//	RenderPassesSettings class
//	Cameraの追加Pass構成を実行用Snapshotとして保持する
//============================================================================
public sealed class RenderPassesSettings : SettingsObject {

	internal RenderPassesSettings(JsonObject values) : base(values) { }
	public bool gameView { get => Read("gameView", true); set => Write("gameView", value); }
	public bool sceneView { get => Read("sceneView", true); set => Write("sceneView", value); }
	public IReadOnlyList<RenderPassSettings> passes => (values["passes"] as JsonArray ?? new()).OfType<JsonObject>().Select(p => new RenderPassSettings(p)).ToArray();
	public RenderPassSettings? FindPass(string name) => passes.FirstOrDefault(p => p.name == name);
	public JsonArray GetHierarchy() => (JsonArray)(values["hierarchy"] as JsonArray ?? new()).DeepClone();
	public void SetHierarchy(JsonArray hierarchy) {
		ArgumentNullException.ThrowIfNull(hierarchy);
		values["hierarchy"] = hierarchy.DeepClone();
	}
	public JsonObject ToJson() => (JsonObject)values.DeepClone();

	// 既存の設定形式を使いCameraの実行用構成へ読み込む
	public void Import(JsonObject settings) {
		ArgumentNullException.ThrowIfNull(settings);
		if (settings["passes"] is not JsonArray) throw new ArgumentException("passes配列がありません");
		var copy = (JsonObject)settings.DeepClone();
		values.Clear();
		foreach (var entry in copy.ToArray()) { copy.Remove(entry.Key); values[entry.Key] = entry.Value; }
	}
}

public sealed class RenderPassSettings : SettingsObject {

	internal RenderPassSettings(JsonObject values) : base(values) { }
	public UUID id => UUID.Parse(Read("id", ""));
	public string name { get => Read("name", ""); set => Write("name", value); }
	public bool enabled { get => Read("enabled", true); set => Write("enabled", value); }
	public RenderPassKind kind { get => ReadEnum("type", RenderPassKind.Compute); set => WriteEnum("type", value); }
	public RenderPassAnchor anchor { get => ReadEnum("anchor", RenderPassAnchor.AfterTransparent); set => WriteEnum("anchor", value); }
	public AssetGUID material { get => ReadAsset("material"); set => WriteAsset("material", value); }
	public string materialPass { get => Read("materialPass", "PostProcess"); set => Write("materialPass", value); }
	public uint rayGenerationIndex { get => Read("rayGenerationIndex", 0u); set => Write("rayGenerationIndex", value); }
	public bool adaptiveResolution { get => Read("adaptiveResolution", false); set => Write("adaptiveResolution", value); }
	public float gpuBudgetMs { get => Read("gpuBudgetMs", 2f); set => Write("gpuBudgetMs", value); }
	public float minResolutionScale { get => Read("minResolutionScale", 0.5f); set => Write("minResolutionScale", value); }
	public float maxResolutionScale { get => Read("maxResolutionScale", 1f); set => Write("maxResolutionScale", value); }
	public float resolutionStep { get => Read("resolutionStep", 0.25f); set => Write("resolutionStep", value); }
	public uint adjustmentIntervalFrames { get => Read("adjustmentIntervalFrames", 30u); set => Write("adjustmentIntervalFrames", value); }
	public IReadOnlyList<RenderPassOutputSettings> outputs => (values["outputs"] as JsonArray ?? new()).OfType<JsonObject>().Select(p => new RenderPassOutputSettings(p)).ToArray();
	public RenderPassSourceKind sourceKind {
		get => Enum.Parse<RenderPassSourceKind>(Child("source")["kind"]?.GetValue<string>() ?? "PreviousPass");
		set => Child("source")["kind"] = value.ToString();
	}
	public void SetSource(RenderPassSourceKind kind, UUID pass = default, string output = "Color") =>
		values["source"] = new JsonObject { ["kind"] = kind.ToString(), ["pass"] = pass.ToString(), ["output"] = output };
	public void SetTexture(string resource, Texture? texture) => Child("textures")[resource] = texture?.assetID.ToString() ?? "";
	public void SetSceneInput(string resource, string input) => Child("sceneInputs")[resource] = input;
	public void SetPassInput(string resource, UUID pass, string output = "Color") =>
		Child("passInputs")[resource] = new JsonObject { ["pass"] = pass.ToString(), ["output"] = output };
	public JsonObject GetSampler(string resource) => (JsonObject)(Child("samplers")[resource] as JsonObject ?? new()).DeepClone();
	public void SetSampler(string resource, JsonObject settings) => Child("samplers")[resource] = settings.DeepClone();
	public JsonObject ToJson() => (JsonObject)values.DeepClone();
}

public sealed class RenderPassOutputSettings : SettingsObject {

	internal RenderPassOutputSettings(JsonObject values) : base(values) { }
	public string name { get => Read("name", "Color"); set => Write("name", value); }
	public string shaderResource { get => Read("shaderResource", "gDestColor"); set => Write("shaderResource", value); }
	public RenderPassOutputFormat format { get => ReadEnum("format", RenderPassOutputFormat.Inherit); set => WriteEnum("format", value); }
	public float widthScale { get => Read("widthScale", 1f); set => Write("widthScale", value); }
	public float heightScale { get => Read("heightScale", 1f); set => Write("heightScale", value); }
	public bool history { get => Read("history", false); set => Write("history", value); }
	public string historyShaderResource { get => Read("historyShaderResource", ""); set => Write("historyShaderResource", value); }
	public Color4? clearColor {
		get => values.ContainsKey("clearColor") ? ReadColor("clearColor", Color4.black) : null;
		set { if (value.HasValue) WriteColor("clearColor", value.Value); else values.Remove("clearColor"); }
	}
}
