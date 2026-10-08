#include "ProjectAssetDocumentPatch.h"

//============================================================================
//	include
//============================================================================
#include "ProjectAssetPath.h"
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonCanonical.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>

namespace Engine {

	bool ProjectAssetDocumentPatch::PrepareJsonAssetName(
		const std::filesystem::path& path, AssetType type, std::string& bytes) {

		if (!AssetTypeResolver::IsJsonAssetFile(type, path)) {
			return true;
		}
		try {
			// 解析成功まで入力byteを変更しない
			auto data = nlohmann::json::parse(bytes);
			if (!data.is_object()) {
				return false;
			}
			const auto name = ProjectAssetPath::SplitAssetFileName(path).first;
			if (type == AssetType::Scene || type == AssetType::Prefab) {
				auto& header = data["Header"];
				if (!header.is_object()) {
					header = nlohmann::json::object();
				}
				header["name"] = name;
			} else {
				data["name"] = name;
			}
			if (JsonCanonical::SerializeCanonical(data, 4).empty()) {
				return false;
			}
			bytes = data.dump(4);
			return true;
		} catch (const nlohmann::json::exception&) {
			return false;
		}
	}

	bool ProjectAssetDocumentPatch::ShouldSkipCopyFile(const std::filesystem::path& path) {

		// metaとその一時ファイルをコピーから除く
		const std::string fileName = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(path.filename()));
		return Engine::Algorithm::EndsWith(fileName, ".meta") || fileName.find(".meta.") != std::string::npos;
	}

	std::vector<std::filesystem::path> ProjectAssetDocumentPatch::BuildAssetSidecarPaths(
		const ProjectAssetEntry& asset, const std::filesystem::path& assetPath) {
		std::vector<std::filesystem::path> result;
		// metaは本体と同じ操作で扱う
		result.emplace_back(ProjectAssetPath::MakeMetaPath(assetPath));
		// モデルの共有ファイルは所有対象に含めない
		if (ModelDocumentReferences::IsDocumentPath(assetPath)) {
			return result;
		}
		for (const std::string& sidecar : asset.sidecarFiles) {
			result.emplace_back(assetPath.parent_path() / Engine::Algorithm::PathFromUTF8(sidecar));
		}
		return result;
	}

}
