#include "ProjectAssetDocumentPatch.h"

//============================================================================
//	include
//============================================================================
#include "ProjectAssetPath.h"
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <system_error>

namespace Engine {

	bool ProjectAssetDocumentPatch::PatchJsonAssetName(const std::filesystem::path& path, AssetType type) {

		// JSON以外のAssetは書き換えない
		if (!AssetTypeResolver::IsJsonAssetFile(type, path)) {
			return true;
		}

		// JSONデータを読み込み失敗した場合は中断
		nlohmann::json data;
		if (!JsonAdapter::TryLoad(path, data) || !data.is_object()) {
			return false;
		}

		// ファイル名から表示名を決める
		const std::string assetName = ProjectAssetPath::SplitAssetFileName(path).first;

		// GUIDはmetaに残し、表示名だけを更新する
		if (type == AssetType::Scene || type == AssetType::Prefab) {
			nlohmann::json& header = data["Header"];
			if (!header.is_object()) {
				header = nlohmann::json::object();
			}
			header["name"] = assetName;
		} else {
			data["name"] = assetName;
		}
		// 保存失敗を操作側へ返す
		return JsonAdapter::Save(path, data);
	}

	bool ProjectAssetDocumentPatch::PatchDuplicatedJsonAsset(const std::filesystem::path& path, AssetType type) {

		// 複製先の表示名を更新する
		return PatchJsonAssetName(path, type);
	}

	bool ProjectAssetDocumentPatch::PatchRenamedJsonAsset(const std::filesystem::path& path, AssetType type) {

		// 改名後の表示名を更新する
		return PatchJsonAssetName(path, type);
	}

	bool ProjectAssetDocumentPatch::PatchDuplicatedDirectoryAssets(const std::filesystem::path& duplicatedDirectory) {

		std::error_code ec;
		// 読み取れないAssetがあれば操作全体を失敗させる
		auto it = std::filesystem::recursive_directory_iterator(duplicatedDirectory, ec);
		const std::filesystem::recursive_directory_iterator end{};
		if (ec) {
			return false;
		}

		for (; it != end; it.increment(ec)) {
			if (ec) {
				return false;
			}
			const bool regularFile = it->is_regular_file(ec);
			if (ec) {
				return false;
			}
			if (!regularFile) {
				continue;
			}
			const std::filesystem::path filePath = it->path();

			// メタファイルなどはスキップ
			if (ShouldSkipCopyFile(filePath)) {
				continue;
			}

			const AssetType type = AssetTypeResolver::GuessByPath(filePath);
			if (!PatchDuplicatedJsonAsset(filePath, type)) {
				return false;
			}
		}
		return !ec;
	}

	bool ProjectAssetDocumentPatch::ShouldSkipCopyFile(const std::filesystem::path& path) {
		// .metaファイルやその一時ファイルをファイル操作の対象から除外するための判定
		const std::string fileName = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(path.filename()));
		return Engine::Algorithm::EndsWith(fileName, ".meta") || fileName.find(".meta.") != std::string::npos;
	}

	std::vector<std::filesystem::path> ProjectAssetDocumentPatch::BuildAssetSidecarPaths(
		const ProjectAssetEntry& asset, const std::filesystem::path& assetPath) {
		std::vector<std::filesystem::path> result;
		// アセット本体に随行する.meta等のパスリストを作成し削除や移動の際に一括処理するために使用
		result.emplace_back(ProjectAssetPath::MakeMetaPath(assetPath));
		for (const std::string& sidecar : asset.sidecarFiles) {
			result.emplace_back(assetPath.parent_path() / Engine::Algorithm::PathFromUTF8(sidecar));
		}
		return result;
	}

} // Engine
