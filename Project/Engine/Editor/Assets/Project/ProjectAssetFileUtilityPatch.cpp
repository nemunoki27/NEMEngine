#include "ProjectAssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <system_error>

namespace Engine {

	void ProjectAssetFileUtility::PatchJsonAssetName(const std::filesystem::path& path, AssetType type, bool) {
		// 対象がScene/Prefab/Material等のJSONベースのアセットでなければスキップ
		if (!AssetTypeResolver::IsJsonAssetType(type)) {
			return;
		}
		// 拡張子がJSONでなければバイナリアセット除外として処理しない
		if (Engine::Algorithm::ToLower(path.extension().string()) != ".json") { return; }

		// JSONデータを読み込み失敗した場合は中断
		nlohmann::json data = Engine::JsonAdapter::Load(path.string(), false);
		if (!data.is_object()) { return; }

		// ファイル名から拡張子を除いた新しいアセット名を取得
		const std::string assetName = SplitAssetFileName(path).first;
		
		// 種類に応じてデータ内部の表示名を更新する、アセットIDは.metaだけで管理する
		if (type == AssetType::Scene || type == AssetType::Prefab) {
			nlohmann::json& header = data["Header"];
			if (!header.is_object()) { header = nlohmann::json::object(); }
			header["name"] = assetName;
		}
		else {
			data["name"] = assetName;
		}
		// 変更後のJSONをファイルへ保存
		Engine::JsonAdapter::Save(path.string(), data);
	}

	void ProjectAssetFileUtility::PatchDuplicatedJsonAsset(const std::filesystem::path& path, AssetType type) {
		// 複製されたアセットの表示名を更新する、新しいGUIDは.meta作成時に発行される
		PatchJsonAssetName(path, type, true);
	}

	void ProjectAssetFileUtility::PatchRenamedJsonAsset(const std::filesystem::path& path, AssetType type) {
		// リネームされたアセットの名前のみを更新しGUIDは維持
		PatchJsonAssetName(path, type, false);
	}

	void ProjectAssetFileUtility::PatchDuplicatedDirectoryAssets(const std::filesystem::path& duplicatedDirectory) {
		std::error_code ec;
		// 指定されたディレクトリを再帰的に走査し、含まれる全JSONアセットの表示名を一括修正
		auto it = std::filesystem::recursive_directory_iterator(duplicatedDirectory, std::filesystem::directory_options::skip_permission_denied, ec);
		const std::filesystem::recursive_directory_iterator end{};
		if (ec) { return; }

		for (; it != end; it.increment(ec)) {
			if (ec) { ec.clear(); continue; }
			if (!it->is_regular_file(ec)) { continue; }
			const std::filesystem::path filePath = it->path();
			
			// メタファイルなどはスキップ
			if (ShouldSkipCopyFile(filePath)) { continue; }
			
			const AssetType type = AssetTypeResolver::GuessByPath(filePath);
			if (AssetTypeResolver::IsJsonAssetType(type)) { 
				PatchDuplicatedJsonAsset(filePath, type); 
			}
		}
	}

	bool ProjectAssetFileUtility::ShouldSkipCopyFile(const std::filesystem::path& path) {
		// .metaファイルやその一時ファイルをファイル操作の対象から除外するための判定
		const std::string fileName = Engine::Algorithm::ToLower(path.filename().string());
		return Engine::Algorithm::EndsWith(fileName, ".meta") || fileName.find(".meta.") != std::string::npos;
	}

	std::vector<std::filesystem::path> ProjectAssetFileUtility::BuildAssetSidecarPaths(const ProjectAssetEntry& asset, const std::filesystem::path& assetPath) {
		std::vector<std::filesystem::path> result;
		// アセット本体に随行する.meta等のパスリストを作成し削除や移動の際に一括処理するために使用
		result.emplace_back(MakeMetaPath(assetPath));
		for (const std::string& sidecar : asset.sidecarFiles) { 
			result.emplace_back(assetPath.parent_path() / sidecar); 
		}
		return result;
	}

} // Engine
