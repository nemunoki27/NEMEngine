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

public abstract class ParticleSettingsObject : SettingsObject {

	internal ParticleSettingsObject(JsonObject values) : base(values) { }
}
