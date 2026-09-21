#include "AssetMetaStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <fstream>

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

		const nlohmann::json data = LoadJsonFileNoThrow(metaFullPath);
		if (!data.is_object() || data.value("schemaVersion", 0u) != kAssetMetaSchemaVersion) {
			return false;
		}

		// guidは厳密にパースし欠落や不正や0はすべて破損扱い
		const std::string guidStr = data.value("guid", "");
		const std::optional<AssetID> parsedGuid = TryParseAssetGUID32Hex(guidStr);
		if (!parsedGuid) {
			return false;
		}
		out.guid = *parsedGuid;

		// 未知typeでも例外にせず、Unknownとして扱う(補正は呼び出し側)
		const std::string typeStr = data.value("type", "Unknown");
		out.type = EnumAdapter<AssetType>::FromString(typeStr).value_or(AssetType::Unknown);
		out.importer = data.value("importer", std::string(ResolveImporterName(out.type)));
		out.importerVersion = data.value("importerVersion", 1u);
		if (const auto it = data.find("settings"); it != data.end() && it->is_object()) {
			out.importerSettings = *it;
		} else {
			out.importerSettings = nlohmann::json::object();
		}

		std::filesystem::path assetFullPath = metaFullPath;
		assetFullPath.replace_extension("");
		out.assetPath = RuntimePaths::ToAssetPath(assetFullPath);
		if (out.assetPath.empty()) {
			return false;
		}
		return true;
	}

	bool WriteMetaFile(const std::filesystem::path& metaFullPath, const AssetMeta& meta) {

		// 既存の .meta を読み、script importer が書く "scripts" 等の未知キーを保持したまま
		// 既知キーだけ更新し、AssetDatabaseがguid採番で書き直してもsidecarの追加情報を壊さない
		nlohmann::json data = LoadJsonFileNoThrow(metaFullPath);
		if (!data.is_object()) {
			data = nlohmann::json::object();
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

		std::ofstream ofs(metaFullPath, std::ios::binary | std::ios::trunc);
		if (!ofs.is_open()) {
			return false;
		}

		// ファイルに書き込む
		ofs << data.dump(2);
		ofs.flush();
		return ofs.good();
	}
}
