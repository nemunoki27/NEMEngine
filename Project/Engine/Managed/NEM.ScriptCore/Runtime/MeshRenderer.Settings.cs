using System.Text.Json.Nodes;

namespace NEMEngine;

public enum MaterialSurfaceMode { Auto, Opaque, Masked, Transparent }

//============================================================================
//	SubMeshSettings class
//	SubMeshごとの表面と座標を変更する
//============================================================================
public sealed class SubMeshSettings : SettingsObject {

	internal SubMeshSettings(JsonObject values) : base(values) { }
	public UUID stableID => UUID.Parse(Read("stableID", ""));
	public string name { get => Read("name", ""); set => Write("name", value); }
	public bool visible { get => Read("visible", true); set => Write("visible", value); }
	public Material? material { get { var id = ReadAsset("material"); return id.isValid ? new Material(id) : null; } set => WriteAsset("material", value?.assetID ?? AssetGUID.None); }
	public MaterialSurfaceMode surfaceMode { get => ReadEnum("surfaceMode", MaterialSurfaceMode.Auto); set => WriteEnum("surfaceMode", value); }
	public float alphaCutoff { get => Read("alphaCutoff", 0.5f); set => Write("alphaCutoff", value); }
	public Vector2 uvPosition { get => ReadVector2("uvPos", Vector2.zero); set => WriteVector2("uvPos", value); }
	public float uvRotation { get => Read("uvRotation", 0f); set => Write("uvRotation", value); }
	public Vector2 uvScale { get => ReadVector2("uvScale", Vector2.one); set => WriteVector2("uvScale", value); }
	public Vector3 localPosition { get => ReadVector3("localPos", Vector3.zero); set => WriteVector3("localPos", value); }
	public Vector3 localRotation { get => ReadVector3("localRotation", Vector3.zero); set => WriteVector3("localRotation", value); }
	public Vector3 localScale { get => ReadVector3("localScale", Vector3.one); set => WriteVector3("localScale", value); }
}

public sealed partial class MeshRenderer {

	public IReadOnlyList<SubMeshSettings> GetSubMeshes() =>
		(ComponentSettingsTransfer.Read(native, componentTypeID)["subMeshes"] as JsonArray ?? new JsonArray())
		.OfType<JsonObject>().Select(value => new SubMeshSettings(value)).ToArray();
	public void SetSubMesh(int index, SubMeshSettings settings) {
		ArgumentNullException.ThrowIfNull(settings);
		if (index < 0) throw new ArgumentOutOfRangeException(nameof(index));
		ComponentSettingsTransfer.Apply(native, componentTypeID, new JsonObject { ["index"] = index, ["value"] = settings.values.DeepClone() });
	}
}
