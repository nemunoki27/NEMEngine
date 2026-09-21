#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	AssetDatabase struct
	//============================================================================
	// アセットのメタデータ
	struct AssetMeta {

		// 識別ID
		AssetID guid{};
		AssetType type = AssetType::Unknown;
		std::string importer;
		uint32_t importerVersion = 1;
		nlohmann::json importerSettings = nlohmann::json::object();

		// アセットのファイルパス
		std::string assetPath;

		// アセットの依存先
		std::vector<AssetID> dependencies;
		uint64_t importHash = 0;
	};

	// データベース構築時に検出した問題の種別
	enum class AssetDatabaseIssueType {

		CorruptMeta,            // .metaが壊れている
		DuplicateGuid,          // 同一GUIDが複数アセットに存在
		DuplicatePath,          // 検索キーが衝突
		OrphanMeta,             // 実体のない.meta
		MissingReference,       // 参照先アセットが存在しない
		ReferenceTypeMismatch,  // 参照先の型が期待と異なる
		UnknownAssetType,       // 種別を判定できない
	};
	// 構築時診断の1件分
	struct AssetDatabaseIssue {

		AssetDatabaseIssueType type;
		AssetID assetID{};
		AssetID referencedAssetID{};
		AssetType expectedType = AssetType::Unknown;
		AssetType actualType = AssetType::Unknown;
		std::string assetPath;
		std::string relatedPath;
		std::string detail;
	};

}
