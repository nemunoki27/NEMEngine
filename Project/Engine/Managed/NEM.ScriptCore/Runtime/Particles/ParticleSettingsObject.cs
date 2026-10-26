using System.Text.Json.Nodes;

namespace NEMEngine;

// 未解決の保存参照もAssetと文書内IDのまま保持する
public readonly struct ParticleEntityReference {

	public readonly AssetGUID sourceAsset;
	public readonly UUID localFileID;
	public readonly bool prefab;

	public ParticleEntityReference(AssetGUID sourceAsset, UUID localFileID, bool prefab = false) {
		this.sourceAsset = sourceAsset;
		this.localFileID = localFileID;
		this.prefab = prefab;
	}

	public static ParticleEntityReference FromGameObject(GameObject? target) {
		if (target == null) return default;
		var identity = NativeEntityAPI.ReadEntityReferenceIdentity(target.native);
		return new(identity.sourceAsset, identity.localFileID, identity.kind == EntityRefKind.Prefab);
	}

	public GameObject? Resolve(Component owner) => new EntityRef(prefab ? EntityRefKind.Prefab : EntityRefKind.Scene,
		sourceAsset, localFileID).Resolve(owner.native);

	internal JsonObject ToJson() => new() {
		["kind"] = prefab ? "Prefab" : "Scene",
		["sourceAsset"] = sourceAsset.isValid ? sourceAsset.ToString() : "",
		["localFileId"] = localFileID.isValid ? localFileID.ToString() : "",
	};

	internal static ParticleEntityReference Read(JsonNode? node) => node is JsonObject value
		? new(AssetGUID.Parse(value["sourceAsset"]?.GetValue<string>()), UUID.Parse(value["localFileId"]?.GetValue<string>()),
			value["kind"]?.GetValue<string>() == "Prefab") : default;
}

// 設定の未公開項目も同じSnapshotへ保持する
public abstract class ParticleSettingsObject {

	internal readonly JsonObject values;
	internal ParticleSettingsObject(JsonObject values) { this.values = values; }

	private protected T Read<T>(string name, T fallback) => values[name] is JsonNode value ? value.GetValue<T>() : fallback;
	private protected void Write<T>(string name, T value) => values[name] = JsonValue.Create(value);
	private protected T ReadEnum<T>(string name, T fallback) where T : struct, Enum =>
		values[name] is JsonNode value && Enum.TryParse<T>(value.GetValue<string>(), out var result) ? result : fallback;
	private protected void WriteEnum<T>(string name, T value) where T : struct, Enum => Write(name, value.ToString());
	private protected JsonObject Child(string name) {
		if (values[name] is not JsonObject child) values[name] = child = new JsonObject();
		return child;
	}
	private protected AssetGUID ReadAsset(string name) => AssetGUID.Parse(values[name]?.GetValue<string>());
	private protected void WriteAsset(string name, AssetGUID value) => Write(name, value.isValid ? value.ToString() : "");
	private protected Vector2 ReadVector2(string name, Vector2 fallback) => values[name] is JsonObject value
		? new(value["x"]!.GetValue<float>(), value["y"]!.GetValue<float>()) : fallback;
	private protected void WriteVector2(string name, Vector2 value) => values[name] = new JsonObject { ["x"] = value.x, ["y"] = value.y };
	private protected Vector3 ReadVector3(string name, Vector3 fallback) => values[name] is JsonObject value
		? new(value["x"]!.GetValue<float>(), value["y"]!.GetValue<float>(), value["z"]!.GetValue<float>()) : fallback;
	private protected void WriteVector3(string name, Vector3 value) => values[name] = new JsonObject { ["x"] = value.x, ["y"] = value.y, ["z"] = value.z };
	private protected Color4 ReadColor(string name, Color4 fallback) => values[name] is JsonObject value
		? new(value["r"]!.GetValue<float>(), value["g"]!.GetValue<float>(), value["b"]!.GetValue<float>(), value["a"]!.GetValue<float>()) : fallback;
	private protected void WriteColor(string name, Color4 value) => values[name] = new JsonObject { ["r"] = value.r, ["g"] = value.g, ["b"] = value.b, ["a"] = value.a };
	private protected Vector4 ReadVector4(string name, Vector4 fallback) => values[name] switch {
		JsonArray value when value.Count == 4 => new(value[0]!.GetValue<float>(), value[1]!.GetValue<float>(), value[2]!.GetValue<float>(), value[3]!.GetValue<float>()),
		JsonObject value => new(value["x"]!.GetValue<float>(), value["y"]!.GetValue<float>(), value["z"]!.GetValue<float>(), value["w"]!.GetValue<float>()),
		_ => fallback,
	};
	private protected void WriteVector4(string name, Vector4 value) => values[name] = new JsonArray(value.x, value.y, value.z, value.w);
}
