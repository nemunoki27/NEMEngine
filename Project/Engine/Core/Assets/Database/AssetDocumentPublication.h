#pragma once

//============================================================================
//	include
//============================================================================
#include "AssetMetadata.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>

// c++
#include <optional>
#include <span>
#include <string>

namespace Engine {

	class AssetDatabase;

	// Asset文書とmetaの保存候補
	struct AssetDocumentChange {

		AssetMeta metadata;
		std::filesystem::path filePath;
		nlohmann::json document;
		nlohmann::json metaDocument;
		std::string fileRevision;
		std::string metaRevision;
		bool canonicalize = false;		  // 既存の保存形式を維持する
		std::optional<std::string> bytes; // 画像などの保存内容
	};

	//============================================================================
	//	AssetDocumentPublication class
	//	複数Assetの文書と索引を同じ操作で確定する
	//============================================================================
	class AssetDocumentPublication {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 保存前に識別子とmeta文書を揃える
		static bool Prepare(const AssetDatabase& database, const std::string& assetPath, AssetType type,
			AssetDocumentChange& out, std::string& diagnostic);
		// 全文書を保存し、検証済みの索引を公開する
		static bool Commit(AssetDatabase& database, std::span<const AssetDocumentChange> changes,
			const JsonFileJournal::Scope& scope, std::string& diagnostic);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 割当を伴わず準備済みの索引へ差し替える
		static void SwapDatabaseState(AssetDatabase& database, AssetDatabase& prepared) noexcept;
	};
}
