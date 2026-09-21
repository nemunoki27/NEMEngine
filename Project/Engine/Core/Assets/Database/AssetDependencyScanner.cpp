#include "AssetDependencyScanner.h"

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

namespace Engine::AssetDependencyScanner {

	const std::unordered_map<std::string, Engine::AssetType>& ReferenceKeyMap() {

		static const std::unordered_map<std::string, Engine::AssetType> kMap = {
			{ "mesh", Engine::AssetType::Mesh },
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
			{ "font", Engine::AssetType::Font },
			{ "atlasTexture", Engine::AssetType::Texture },
			{ "audioClip", Engine::AssetType::Audio },
			{ "script", Engine::AssetType::Script },
			{ "scriptAsset", Engine::AssetType::Script },
			{ "prefab", Engine::AssetType::Prefab },
			{ "prefabAsset", Engine::AssetType::Prefab },
			{ "effect", Engine::AssetType::ParticleEffect },
			{ "shader", Engine::AssetType::Shader },
			{ "shaderOverride", Engine::AssetType::Shader },
			{ "sourceShader", Engine::AssetType::Shader },
			{ "functionFileAsset", Engine::AssetType::Shader },
			{ "file", Engine::AssetType::Shader },
			{ "pipeline", Engine::AssetType::RenderPipeline },
			{ "renderFeatureProfile", Engine::AssetType::RenderFeatureProfile },
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
		std::unordered_map<Engine::AssetID, Engine::AssetType>& outIDs,
		std::unordered_map<std::string, Engine::AssetType>& outPaths) {

		if (!value.is_string()) {
			return;
		}
		const std::string reference = value.get<std::string>();
		const std::optional<Engine::AssetID> parsed =
			Engine::TryParseAssetGUID32Hex(reference);
		if (parsed) {
			// 同一IDが複数キーで現れた場合は最初の期待型を維持する
			outIDs.emplace(*parsed, expectedType);
			return;
		}
		if (expectedType != Engine::AssetType::Unknown &&
			!reference.empty()) {

			outPaths.emplace(reference, expectedType);
		}
	}

	void ScanReferences(const nlohmann::json& node,
		std::unordered_map<Engine::AssetID, Engine::AssetType>& outIDs,
		std::unordered_map<std::string, Engine::AssetType>& outPaths) {

		if (node.is_object()) {

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
			if (node.value("type", std::string{}) == "Texture2D" &&
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
				ScanReferences(*it, outIDs, outPaths);
			}
		} else if (node.is_array()) {

			for (const auto& element : node) {
				ScanReferences(element, outIDs, outPaths);
			}
		}
	}
}
