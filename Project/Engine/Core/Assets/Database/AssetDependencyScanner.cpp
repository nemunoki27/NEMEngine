#include "AssetDependencyScanner.h"

//============================================================================
//	AssetDependencyScanner functions
//============================================================================

namespace Engine::AssetDependencyScanner {

	// 同じ参照先でも異なる期待型を捨てずに検査へ渡す
	template<class Map, class Key>
	void AddReference(Map& references, const Key& key, AssetType type) {

		auto [entry, end] = references.equal_range(key);
		while (entry != end) {
			if (entry->second == type || type == AssetType::Unknown) {
				return;
			}
			if (entry->second == AssetType::Unknown) {
				entry = references.erase(entry);
			} else {
				++entry;
			}
		}
		references.emplace(key, type);
	}

	const std::unordered_map<std::string, Engine::AssetType>& ReferenceKeyMap() {

		static const std::unordered_map<std::string, Engine::AssetType> kMap = {
			{ "mesh", Engine::AssetType::Mesh },
			{ "model", Engine::AssetType::Mesh },
			{ "material", Engine::AssetType::Material },
			{ "materials", Engine::AssetType::Material },
			{ "materialGuid", Engine::AssetType::Material },
			{ "shaderGraph", Engine::AssetType::ShaderGraph },
			{ "subGraph", Engine::AssetType::ShaderGraph },
			{ "texture", Engine::AssetType::Texture },
			{ "baseColorTexture", Engine::AssetType::Texture },
			{ "normalTexture", Engine::AssetType::Texture },
			{ "metallicRoughnessTexture", Engine::AssetType::Texture },
			{ "metallicTexture", Engine::AssetType::Texture },
			{ "roughnessTexture", Engine::AssetType::Texture },
			{ "displacementTexture", Engine::AssetType::Texture },
			{ "emissiveTexture", Engine::AssetType::Texture },
			{ "occlusionTexture", Engine::AssetType::Texture },
			{ "specularTexture", Engine::AssetType::Texture },
			{ "opacityTexture", Engine::AssetType::Texture },
			{ "font", Engine::AssetType::Font },
			{ "atlasTexture", Engine::AssetType::Texture },
			{ "audioClip", Engine::AssetType::Audio },
			{ "sound", Engine::AssetType::Audio },
			{ "script", Engine::AssetType::Script },
			{ "scriptAsset", Engine::AssetType::Script },
			{ "prefab", Engine::AssetType::Prefab },
			{ "prefabAsset", Engine::AssetType::Prefab },
			{ "PrefabAsset", Engine::AssetType::Prefab },
			{ "effect", Engine::AssetType::ParticleEffect },
			{ "shader", Engine::AssetType::Shader },
			{ "shaderOverride", Engine::AssetType::Shader },
			{ "sourceShader", Engine::AssetType::Shader },
			{ "functionFileAsset", Engine::AssetType::Shader },
			{ "file", Engine::AssetType::Shader },
			{ "pipeline", Engine::AssetType::RenderPipeline },
			{ "targetTexture", Engine::AssetType::RenderTexture },
			{ "volumeProfile", Engine::AssetType::VolumeProfile },
			{ "renderExtension", Engine::AssetType::RenderExtension },
			{ "animationClip", Engine::AssetType::AnimationClip },
			{ "scene", Engine::AssetType::Scene },
			{ "activeScene", Engine::AssetType::Scene },
			{ "assetId", Engine::AssetType::Unknown },
			{ "controller", Engine::AssetType::Unknown },
		};
		return kMap;
	}

	void TryCollectReference(const nlohmann::json& value,
		Engine::AssetType expectedType,
		IDReferences& outIDs, PathReferences& outPaths) {

		// 参照キー内のobjectは実行時の読込形式と揃える
		if (value.is_object()) {
			for (const char* key : { "guid", "assetGuid", "asset", "id", "assetId" }) {
				const auto found = value.find(key);
				if (found != value.end() && found->is_string() && !found->get_ref<const std::string&>().empty()) {
					TryCollectReference(*found, expectedType, outIDs, outPaths);
					return;
				}
			}
			return;
		}
		if (!value.is_string()) {
			return;
		}
		const std::string reference = value.get<std::string>();
		const std::optional<Engine::AssetID> parsed =
			Engine::TryParseAssetGUID32Hex(reference);
		if (parsed) {
			// 型なし候補より保存形式から確定した型を優先する
			AddReference(outIDs, *parsed, expectedType);
			return;
		}
		if (expectedType != Engine::AssetType::Unknown &&
			!reference.empty()) {

			AddReference(outPaths, reference, expectedType);
		}
	}

	void ScanReferences(const nlohmann::json& node,
		IDReferences& outIDs, PathReferences& outPaths, bool includeUnclassified) {

		if (node.is_object()) {

			// VolumeComponentのProfile参照だけをShader profileと区別して収集
			if (node.contains("profile") && node.contains("blendDistance") &&
				node.contains("global") && node.contains("layerMask")) {

				TryCollectReference(node["profile"], AssetType::VolumeProfile, outIDs, outPaths);
			}

			// clip名を使うSkinnedAnimationとAsset参照を区別する
			if (node.contains("clip")) {
				if (node.contains("playOnAwake") && node.contains("volume")) {
					TryCollectReference(node["clip"], AssetType::Audio, outIDs, outPaths);
				} else if (node.contains("wrapMode") && node.contains("relativeTransform")) {
					TryCollectReference(node["clip"], AssetType::AnimationClip, outIDs, outPaths);
				}
			}
			// 任意名のTexture overrideも依存先へ含める
			for (const char* key : { "textureOverrides", "textures" }) {
				const auto found = node.find(key);
				if (found != node.end() && found->is_object()) {
					for (const auto& texture : *found) {
						TryCollectReference(texture, AssetType::Texture, outIDs, outPaths);
					}
				}
			}
			// EntityRefの所有アセットもシーン削除時の参照保護に含める
			if (node.contains("kind") && node.contains("sourceAsset") && node.contains("localFileId")) {
				TryCollectReference(node["sourceAsset"], Engine::AssetType::Unknown, outIDs, outPaths);
			}

			// Material Instanceのrecord形式ではTexture GUIDがvalue配下に保存される
			if (node.contains("id") &&
				node.contains("name") &&
				node.contains("value")) {

				TryCollectReference(
					node["value"],
					Engine::AssetType::Texture,
					outIDs, outPaths);
			}
			// Shader GraphのTexture2D既定値も生成Material作成前から依存として保持する
			if (node.contains("type") && node["type"].is_string() && node["type"] == "Texture2D" &&
				node.contains("defaultValue")) {

				TryCollectReference(
					node["defaultValue"],
					Engine::AssetType::Texture,
					outIDs, outPaths);
			}

			const auto& keyMap = ReferenceKeyMap();
			for (auto it = node.begin(); it != node.end(); ++it) {

				const auto found = keyMap.find(it.key());
				if (found != keyMap.end()) {

					if (it->is_array()) {
						for (const auto& element : *it) {
							TryCollectReference(element, found->second,
								outIDs, outPaths);
						}
					} else {
						TryCollectReference(*it, found->second,
							outIDs, outPaths);
					}
				}
				// 参照キーでなくても、ネストした参照を拾うため再帰する
				ScanReferences(*it, outIDs, outPaths, includeUnclassified);
			}
		} else if (node.is_array()) {

			for (const auto& element : node) {
				ScanReferences(element, outIDs, outPaths, includeUnclassified);
			}
		} else if (includeUnclassified && node.is_string()) {

			// Buildは既存Assetと照合するため型なしGUIDも候補にする
			TryCollectReference(node, AssetType::Unknown, outIDs, outPaths);
		}
	}
}
