#include "AssetMaintenance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace Engine::AssetMaintenance {

	// 有効なTexture参照は修復対象から除外する
	static bool HasValidAtlas(const AssetDatabase& database, const nlohmann::json& data) {

		const auto value = data.find("atlasTexture");
		if (value == data.end() || !value->is_string()) {
			return false;
		}
		const auto id = TryParseAssetGUID32Hex(value->get<std::string>());
		const auto* meta = id ? database.Find(*id) : nullptr;
		return meta && meta->type == AssetType::Texture && std::filesystem::is_regular_file(database.ResolveFullPath(*id));
	}

	void DetectFontAtlasReferences(const AssetDatabase& database, std::vector<AssetDatabaseIssue>& issues) {

		const std::string suffix = ".font.json";
		for (const auto& [id, meta] : database.GetAssets()) {
			if (meta.type != AssetType::Font || !Algorithm::EndsWith(meta.assetPath, suffix)) {
				continue;
			}
			const auto data = AssetFileUtility::LoadJsonFileNoThrow(database.ResolveFullPath(id));
			if (!data.is_object() || HasValidAtlas(database, data)) {
				continue;
			}
			// 同名画像は候補として表示し、自動では書き換えない
			const auto atlasPath = meta.assetPath.substr(0, meta.assetPath.size() - suffix.size()) + ".png";
			const auto* atlas = database.FindByPath(atlasPath);
			const AssetID atlasID = atlas && atlas->type == AssetType::Texture ? atlas->guid : AssetID{};
			issues.push_back({ AssetDatabaseIssueType::FontAtlasRepair, id, atlasID,
				AssetType::Texture, atlasID ? AssetType::Texture : AssetType::Unknown, meta.assetPath, atlasPath,
				atlasID ? "Atlas参照が欠損しています 修復候補を確認してください" : "Atlas参照と修復候補が見つかりません" });
		}
	}

	void DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots, std::vector<AssetDatabaseIssue>& issues) {

		for (const auto& root : scanRoots) {
			for (auto it = std::filesystem::recursive_directory_iterator(root);
				it != std::filesystem::recursive_directory_iterator{}; ++it) {
				if (it->is_directory()) {
					if (AssetFileUtility::IsExternalActorsDirectory(it->path()) ||
						AssetFileUtility::IsAssetCopyStagingDirectory(it->path())) {
						it.disable_recursion_pending();
					}
					continue;
				}
				const auto metaPath = it->path();
				const auto name = Algorithm::PathToUTF8(metaPath.filename());
				if (!it->is_regular_file() || !Algorithm::EndsWith(name, ".meta") || name.find(".meta.") != std::string::npos) {
					continue;
				}
				auto assetPath = metaPath;
				assetPath.replace_extension();
				if (!std::filesystem::exists(assetPath)) {
					issues.push_back({ AssetDatabaseIssueType::OrphanMeta, {}, {}, AssetType::Unknown, AssetType::Unknown,
						Algorithm::PathToUTF8(assetPath), Algorithm::PathToUTF8(metaPath), "元Assetが見つかりません" });
				}
			}
		}
	}

	bool RepairFontAtlas(const AssetDatabase& database, AssetID fontID, AssetID atlasID) {

		const auto* font = database.Find(fontID);
		const auto* atlas = database.Find(atlasID);
		if (!font || font->type != AssetType::Font || !atlas || atlas->type != AssetType::Texture) {
			return false;
		}
		const auto path = database.ResolveFullPath(fontID);
		auto data = AssetFileUtility::LoadJsonFileNoThrow(path);
		// 検出後に修復済みになった文書や消えた候補は変更しない
		if (!data.is_object() || HasValidAtlas(database, data) || !std::filesystem::is_regular_file(database.ResolveFullPath(atlasID))) {
			return false;
		}
		data["atlasTexture"] = ToString(atlasID);
		return JsonFile::SaveCanonical(path, data, 2);
	}
}
