#include "AssetMaintenance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <fstream>

namespace Engine::AssetMaintenance {

	using AssetFileUtility::LoadJsonFileNoThrow;
	using AssetFileUtility::IsExternalActorsDirectory;

	void ReconcileFontAtlasReferences(const AssetDatabase& database) {

		// .font.jsonのatlasTextureを隣接する同名アトラス画像の現在GUIDへ揃えて書き戻す
		// フォントを.meta無しでコピーするとGUIDが再採番され参照が切れるため、永続的に直す
		const std::string fontSuffix = ".font.json";
		for (const auto& [guid, meta] : database.GetAssets()) {

			if (meta.type != AssetType::Font || !Algorithm::EndsWith(meta.assetPath, fontSuffix)) {
				continue;
			}
			// <name>.font.jsonと同じ場所の<name>.pngをアトラスとする
			const std::string atlasPath = meta.assetPath.substr(0, meta.assetPath.size() - fontSuffix.size()) + ".png";
			const AssetMeta* atlasMeta = database.FindByPath(atlasPath);
			if (!atlasMeta) {
				continue;
			}
			const std::filesystem::path fullPath = database.ResolveFullPath(guid);
			nlohmann::json data = LoadJsonFileNoThrow(fullPath);
			if (!data.is_object()) {
				continue;
			}
			// 既に有効なTextureのGUIDを指しているなら尊重して触らない、ここが冪等性も担保する
			if (const std::optional<AssetID> currentGuid = TryParseAssetGUID32Hex(data.value("atlasTexture", std::string{}))) {
				const AssetMeta* current = database.Find(*currentGuid);
				if (current && current->type == AssetType::Texture) {
					continue;
				}
			}

			// 参照が切れている(パス指定/空/未登録GUID)ので隣接アトラスのGUIDへ直す
			data["atlasTexture"] = ToString(atlasMeta->guid);
			std::ofstream ofs(fullPath, std::ios::binary | std::ios::trunc);
			if (!ofs.is_open()) {
				continue;
			}
			ofs << data.dump(2);
			Logger::Output(LogType::Engine, spdlog::level::info,
				"[AssetDatabase] Font Atlasを再接続しました Font={} Atlas={}", meta.assetPath, atlasPath);
		}
	}

	void DetectOrphanMeta(const std::vector<std::filesystem::path>& scanRoots) {

		// 走査中にファイルを消すとiteratorが壊れるので、先に孤立.metaを集めてから削除する
		std::vector<std::filesystem::path> orphanMetas;

		for (const std::filesystem::path& scanRoot : scanRoots) {

			std::error_code ec;
			if (!std::filesystem::exists(scanRoot, ec) || !std::filesystem::is_directory(scanRoot, ec)) {
				continue;
			}

			auto it = std::filesystem::recursive_directory_iterator(
				scanRoot, std::filesystem::directory_options::skip_permission_denied, ec);
			const std::filesystem::recursive_directory_iterator end{};
			if (ec) {
				continue;
			}

			for (; it != end; it.increment(ec)) {

				if (ec) {
					ec.clear();
					continue;
				}
				if (it->is_directory(ec)) {

					if (IsExternalActorsDirectory(it->path())) {
						it.disable_recursion_pending();
					}
					continue;
				}
				if (!it->is_regular_file(ec)) {
					continue;
				}

				const std::filesystem::path metaPath = it->path();
				// "<asset>.meta" のみを対象にする(.meta.バックアップ等は対象外)
				const std::string filename = Algorithm::PathToUTF8(metaPath.filename());
				if (!Algorithm::EndsWith(filename, ".meta") || filename.find(".meta.") != std::string::npos) {
					continue;
				}

				std::filesystem::path assetFull = metaPath;
				assetFull.replace_extension("");
				if (!std::filesystem::exists(assetFull, ec)) {
					orphanMetas.emplace_back(metaPath);
				}
			}
		}

		// 元アセットが消えた孤立.metaは自動削除する、読み取り専用などで消せなければ警告だけ出す
		for (const std::filesystem::path& metaPath : orphanMetas) {

			std::error_code ec;
			if (std::filesystem::remove(metaPath, ec)) {

				Logger::Output(LogType::Engine, "[AssetDatabase] 孤立した.metaを削除しました path={}",
					Algorithm::PathToUTF8(metaPath));
			} else {

				Logger::Output(LogType::Engine, spdlog::level::warn,
					"[AssetDatabase] 孤立した.metaを削除できません path={}",
					Algorithm::PathToUTF8(metaPath));
			}
		}
	}
}
