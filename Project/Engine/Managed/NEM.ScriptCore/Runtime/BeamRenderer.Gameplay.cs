using System.Text.Json.Nodes;

namespace NEMEngine;

public enum BeamMode { Beam, Lightning }
public enum BeamEndpointMode { Local, World, Object }
public enum BeamPlaybackMode { Continuous, OneShot }

public readonly record struct BeamEndpoint(BeamEndpointMode Mode, Vector3 Position, GameObject? Object = null) {

	public static BeamEndpoint Local(Vector3 position) => new(BeamEndpointMode.Local, position);
	public static BeamEndpoint World(Vector3 position) => new(BeamEndpointMode.World, position);
	public static BeamEndpoint OnObject(GameObject? target, Vector3 offset = default) => new(BeamEndpointMode.Object, offset, target);
}

public sealed class BeamAppearance : SettingsObject {

	internal BeamAppearance(JsonObject values) : base(values) { }
	public CurveSettings WidthCurve => new(Child("widthCurve"));

	internal static void Apply(NativeEntity native, int componentTypeID, BeamAppearance appearance) {
		ArgumentNullException.ThrowIfNull(appearance);
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject {
			["widthCurve"] = appearance.values["widthCurve"]!.DeepClone()
		});
	}
}

public sealed unsafe partial class BeamRenderer {

	public BeamMode Mode {
		get => Enum.Parse<BeamMode>(Read<string>("mode"));
		set => Write("mode", value.ToString());
	}

	public bool Is2D {
		get => Read<bool>("is2D");
		set => Write("is2D", value);
	}

	public bool UseWidthCurve {
		get => Read<bool>("useWidthCurve");
		set => Write("useWidthCurve", value);
	}

	public float Width {
		get => Read<float>("width");
		set => Write("width", value);
	}

	public float WidthMultiplier {
		get => Read<float>("widthMultiplier");
		set => Write("widthMultiplier", value);
	}

	public int Subdivisions {
		get => Read<int>("subdivisions");
		set => Write("subdivisions", value);
	}

	public float Amplitude {
		get => Read<float>("amplitude");
		set => Write("amplitude", value);
	}

	public float Frequency {
		get => Read<float>("frequency");
		set => Write("frequency", value);
	}

	public uint Seed {
		get => Read<uint>("seed");
		set => Write("seed", value);
	}

	public int BranchCount {
		get => Read<int>("branchCount");
		set => Write("branchCount", value);
	}

	public float BranchLength {
		get => Read<float>("branchLength");
		set => Write("branchLength", value);
	}

	public float BranchWidth {
		get => Read<float>("branchWidth");
		set => Write("branchWidth", value);
	}

	public BeamPlaybackMode PlaybackMode {
		get => Enum.Parse<BeamPlaybackMode>(Read<string>("playbackMode"));
		set => Write("playbackMode", value.ToString());
	}

	public float Duration {
		get => Read<float>("duration");
		set => Write("duration", value);
	}

	public bool PlayOnStart {
		get => Read<bool>("playOnStart");
		set => Write("playOnStart", value);
	}

	public bool StopOnHit {
		get => Read<bool>("stopOnHit");
		set => Write("stopOnHit", value);
	}

	public uint CollisionMask {
		get => Read<uint>("collisionMask");
		set => Write("collisionMask", value);
	}

	public QueryTriggerInteraction TriggerInteraction {
		get => Enum.Parse<QueryTriggerInteraction>(Read<string>("triggerInteraction"));
		set => Write("triggerInteraction", value.ToString());
	}

	public bool Visible {
		get => Read<bool>("visible");
		set => Write("visible", value);
	}

	public uint RenderingLayerMask {
		get => Read<uint>("renderingLayerMask");
		set => Write("renderingLayerMask", value);
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

	public MaterialInstance MaterialInstance => new(gameObject, RendererMaterialTarget.Beam);

	public Material? Material {
		get {
			var id = AssetGUID.Parse(Read<string>("material"));
			return id.isValid ? new Material(id) : null;
		}
		set => Write("material", value?.assetID.ToString() ?? "");
	}

	public TimeSourceSettings TimeSource {
		get => ComponentTimeSource.Read(native, componentTypeID);
		set => ComponentTimeSource.Apply(native, componentTypeID, value);
	}

	public RendererRenderSettings GetRenderSettings() => ComponentRenderSettings.Read(native, componentTypeID);
	public void SetRenderSettings(RendererRenderSettings settings) => ComponentRenderSettings.Apply(native, componentTypeID, settings);

	public BeamAppearance GetAppearance() => new(ComponentSettingsTransfer.Read(native, componentTypeID));
	public void SetAppearance(BeamAppearance appearance) => BeamAppearance.Apply(native, componentTypeID, appearance);

	public BeamEndpoint Start {
		get => ReadPoint(-1);
		set => WritePoint(-1, value);
	}

	public BeamEndpoint End {
		get => ReadPoint(-2);
		set => WritePoint(-2, value);
	}

	public int WaypointCount => Command(3);
	public BeamEndpoint GetWaypoint(int index) => ReadPoint(ValidateIndex(index));
	public void SetWaypoint(int index, BeamEndpoint point) => WritePoint(ValidateIndex(index), point);
	public void AddWaypoint(BeamEndpoint point) => WritePoint(-3, point);
	public void RemoveWaypoint(int index) {
		if (NativeAPI.EditBeamWaypoints(native, 0, index, 0) == 0) throw new ArgumentOutOfRangeException(nameof(index));
	}
	public void MoveWaypoint(int from, int to) {
		if (NativeAPI.EditBeamWaypoints(native, 1, from, to) == 0) throw new ArgumentOutOfRangeException();
	}

	public void Play() => Command(0);
	public void Stop() => Command(1);
	public bool IsPlaying => Command(2) != 0;

	public bool TryGetHit(out RaycastHit hit, out int segmentIndex) {
		NativeRaycastHit result = default;
		int segment = -1;
		bool found = NativeAPI.GetBeamHit(native, &result, &segment) != 0;
		hit = found ? RaycastHit.From(result) : default;
		segmentIndex = segment;
		return found;
	}

	private static int ValidateIndex(int index) => index < 0 ? throw new ArgumentOutOfRangeException(nameof(index)) : index;

	private BeamEndpoint ReadPoint(int index) {
		int mode;
		NativeVector3 position;
		NativeEntity target;
		if (NativeAPI.ReadBeamEndpoint(native, index, &mode, &position, &target) == 0) throw new ArgumentOutOfRangeException(nameof(index));
		return new((BeamEndpointMode)mode, position.ToVector3(), GameObject.FromNative(target));
	}

	private void WritePoint(int index, BeamEndpoint point) {
		if (NativeAPI.WriteBeamEndpoint(native, index, (int)point.Mode, NativeVector3.From(point.Position), GameObject.RawNative(point.Object)) == 0)
			throw new ArgumentException("座標、経由点の番号、対象オブジェクトのシーンを確認してください", nameof(point));
	}

	private int Command(int command) {
		int result = NativeAPI.BeamCommand(native, command);
		if (result < 0) throw new InvalidOperationException("BeamRendererを取得できません");
		return result;
	}

	private T Read<T>(string key) => ComponentSettingsTransfer.Read(native, componentTypeID)[key]!.GetValue<T>();
	private void Write<T>(string key, T value) =>
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject { [key] = JsonValue.Create(value) });
}
