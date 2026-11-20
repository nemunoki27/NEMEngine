using System.Text.Json.Nodes;

namespace NEMEngine;

public enum TrailMode { SinglePoint, TwoPoints }
public enum TrailAlignment { View, TransformZ }
public enum TrailTextureMode { Stretch, Tile }

// 曲線の編集結果はSetAppearanceでまとめて適用する
public sealed class TrailAppearance : SettingsObject {

	internal TrailAppearance(JsonObject values) : base(values) { }
	public CurveSettings WidthCurve => new(Child("widthCurve"));
	public CurveChannelsSettings ColorGradient => new(values, "colorGradient", 4);

	internal static void Apply(NativeEntity native, int componentTypeID, TrailAppearance appearance) {
		ArgumentNullException.ThrowIfNull(appearance);
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject {
			["widthCurve"] = appearance.values["widthCurve"]!.DeepClone(),
			["colorGradient"] = appearance.values["colorGradient"]!.DeepClone()
		});
	}
}

public sealed unsafe partial class TrailRenderer {

	public TrailMode Mode {
		get => Enum.Parse<TrailMode>(Read<string>("mode"));
		set => Write("mode", value.ToString());
	}

	public TrailAlignment Alignment {
		get => Enum.Parse<TrailAlignment>(Read<string>("alignment"));
		set => Write("alignment", value.ToString());
	}

	public TrailTextureMode TextureMode {
		get => Enum.Parse<TrailTextureMode>(Read<string>("textureMode"));
		set => Write("textureMode", value.ToString());
	}

	public bool Is2D {
		get => Read<bool>("is2D");
		set => Write("is2D", value);
	}

	public float Time {
		get => Read<float>("lifetime");
		set => Write("lifetime", value);
	}

	public float MinVertexDistance {
		get => Read<float>("minVertexDistance");
		set => Write("minVertexDistance", value);
	}

	public float Width {
		get => Read<float>("width");
		set => Write("width", value);
	}

	public float WidthMultiplier {
		get => Read<float>("widthMultiplier");
		set => Write("widthMultiplier", value);
	}

	public bool Visible {
		get => Read<bool>("visible");
		set => Write("visible", value);
	}

	public int Layer {
		get => Read<int>("layer");
		set => Write("layer", value);
	}

	public int Order {
		get => Read<int>("order");
		set => Write("order", value);
	}

	public BlendMode BlendMode {
		get => Enum.Parse<BlendMode>(Read<string>("blendMode"));
		set => Write("blendMode", value.ToString());
	}

	public RenderPhase Queue {
		get => Enum.Parse<RenderPhase>(Read<string>("queue"));
		set => Write("queue", value.ToString());
	}

	public uint RenderingLayerMask {
		get => Read<uint>("renderingLayerMask");
		set => Write("renderingLayerMask", value);
	}

	public MaterialInstance MaterialInstance => new(gameObject, RendererMaterialTarget.Trail);

	public Material? Material {
		get {
			var id = AssetGUID.Parse(Read<string>("material"));
			return id.isValid ? new Material(id) : null;
		}
		set => Write("material", value?.assetID.ToString() ?? "");
	}

	public bool Emitting {
		get => Command(5) != 0;
		set => Command(value ? 3 : 4);
	}
	public int PositionCount => Command(6);

	public GameObject? Root {
		get => GameObject.FromNative(NativeAPI.GetTrailEndpoint(native, 0));
		set => SetEndpoint(0, value);
	}
	public GameObject? Tip {
		get => GameObject.FromNative(NativeAPI.GetTrailEndpoint(native, 1));
		set => SetEndpoint(1, value);
	}

	public TimeSourceSettings TimeSource {
		get => ComponentTimeSource.Read(native, componentTypeID);
		set => ComponentTimeSource.Apply(native, componentTypeID, value);
	}

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);

	public TrailAppearance GetAppearance() => new(ComponentSettingsTransfer.Read(native, componentTypeID));
	public void SetAppearance(TrailAppearance appearance) => TrailAppearance.Apply(native, componentTypeID, appearance);

	public void Clear() => Command(0);
	public void AddPosition(Vector3 position) => Command(1, position);
	public void AddSegment(Vector3 root, Vector3 tip) => Command(2, root, tip);

	private T Read<T>(string key) => ComponentSettingsTransfer.Read(native, componentTypeID)[key]!.GetValue<T>();
	private void Write<T>(string key, T value) =>
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject { [key] = JsonValue.Create(value) });

	private int Command(int command, Vector3 a = default, Vector3 b = default) {
		int result = NativeAPI.TrailCommand(native, command, NativeVector3.From(a), NativeVector3.From(b));
		if (result < 0) throw new InvalidOperationException("TrailRendererの操作に失敗しました。描画方式と座標を確認してください");
		return result;
	}

	private void SetEndpoint(int tip, GameObject? point) {
		if (NativeAPI.SetTrailEndpoint(native, tip, GameObject.RawNative(point)) == 0)
			throw new ArgumentException("同じシーンの有効なオブジェクトを指定してください", nameof(point));
	}
}
