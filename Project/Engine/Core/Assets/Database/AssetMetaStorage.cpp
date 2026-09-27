#include "AssetMetaStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <limits>

namespace Engine::AssetMetaStorage {

	constexpr uint32_t kAssetMetaSchemaVersion = 2;
	using AssetFileUtility::LoadJsonFileNoThrow;

	std::string_view ResolveImporterName(Engine::AssetType type) {

		switch (type) {
		case Engine::AssetType::Texture:          return "TextureImporter";
		case Engine::AssetType::Mesh:             return "MeshImporter";
		case Engine::AssetType::Audio:            return "AudioImporter";
		case Engine::AssetType::Script:           return "ScriptImporter";
		case Engine::AssetType::Font:             return "FontImporter";
		case Engine::AssetType::Scene:            return "SceneImporter";
		case Engine::AssetType::Prefab:           return "PrefabImporter";
		case Engine::AssetType::Material:         return "MaterialImporter";
		case Engine::AssetType::Shader:           return "ShaderImporter";
		case Engine::AssetType::RenderPipeline:   return "RenderPipelineImporter";
		case Engine::AssetType::AnimationClip:    return "AnimationClipImporter";
		case Engine::AssetType::ParticleEffect:   return "ParticleEffectImporter";
		case Engine::AssetType::ShaderGraph:      return "ShaderGraphImporter";
		case Engine::AssetType::RenderFeatureProfile:
			return "RenderFeatureProfileImporter";
		default:                                  return "DefaultImporter";
		}
	}

	std::filesystem::path MetaPathOf(const std::filesystem::path& assetFullPath) {

		std::filesystem::path metaPath = assetFullPath;
		metaPath += L".meta";
		return metaPath;
	}

	bool ReadMetaFile(const std::filesystem::path& metaFullPath, AssetMeta& out) {

		AssetMeta loaded;
		try {

			const nlohmann::json data = LoadJsonFileNoThrow(metaFullPath);
			if (!data.is_object() || !data.contains("schemaVersion") || !data["schemaVersion"].is_number_integer() ||
				data["schemaVersion"] != kAssetMetaSchemaVersion) {
				return false;
			}

			// guidは厳密にパースし欠落や不正や0はすべて破損扱い
			const std::string guidStr = data.value("guid", "");
			const std::optional<AssetID> parsedGuid = TryParseAssetGUID32Hex(guidStr);
			if (!parsedGuid) {
				return false;
			}
			loaded.guid = *parsedGuid;

			// 誤記された型をUnknownへ読み替えて上書きしない
			const std::string typeStr = data.value("type", "Unknown");
			const auto type = EnumAdapter<AssetType>::FromString(typeStr);
			if (!type) {
				return false;
			}
			loaded.type = *type;
			loaded.importer = data.value("importer", std::string(ResolveImporterName(loaded.type)));
			if (const auto version = data.find("importerVersion"); version != data.end()) {
				if (!version->is_number_integer() || *version < 1 ||
					*version > (std::numeric_limits<uint32_t>::max)()) {
					return false;
				}
			}
			loaded.importerVersion = data.value("importerVersion", 1u);
			if (const auto it = data.find("settings"); it != data.end()) {
				if (!it->is_object()) {
					return false;
				}
				loaded.importerSettings = *it;
			} else {
				loaded.importerSettings = nlohmann::json::object();
			}

			std::filesystem::path assetFullPath = metaFullPath;
			assetFullPath.replace_extension("");
			loaded.assetPath = RuntimePaths::ToAssetPath(assetFullPath);
			if (loaded.assetPath.empty()) {
				return false;
			}
			out = std::move(loaded);
			return true;
		} catch (const nlohmann::json::exception&) {
			return false;
		}
	}

	bool WriteMetaFile(const std::filesystem::path& metaFullPath, const AssetMeta& meta) {

		if (!meta.guid || meta.importerVersion == 0 || !meta.importerSettings.is_object()) {
			return false;
		}
		// 未知キーを保持し、破損した既存metaを空文書で上書きしない
		nlohmann::json data = nlohmann::json::object();
		std::error_code error;
		if (std::filesystem::exists(metaFullPath, error) &&
			(!JsonFile::TryLoad(metaFullPath, data) || !data.is_object())) {
			return false;
		}
		if (error) {
			return false;
		}

		data["schemaVersion"] = kAssetMetaSchemaVersion;
		data["guid"] = ToString(meta.guid);
		data["type"] = std::string(EnumAdapter<AssetType>::ToString(meta.type));
		data["importer"] = meta.importer.empty() ?
			std::string(ResolveImporterName(meta.type)) : meta.importer;
		data["importerVersion"] = meta.importerVersion;
		data["settings"] = meta.importerSettings.is_object() ?
			meta.importerSettings : nlohmann::json::object();
		data.erase("version");

		// 全量を書き込んでから既存ファイルと置き換える
		return JsonFile::SaveCanonical(metaFullPath, data, 2);
	}
}
