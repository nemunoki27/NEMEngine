#pragma once

//============================================================================
//	include
//============================================================================
#include "AssetMetadata.h"
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>

// c++
#include <span>

namespace Engine {

	// 文書とmetaを一組で保存する用途
	enum class AssetDocumentSaveKind {

		Material,
		ShaderGraph,
		GeneratedRender,
		Prefab,
		Font,
	};

	//============================================================================
	//	AssetDocumentRecovery namespace
	//============================================================================
	namespace AssetDocumentRecovery {

		// 保存と復旧で扱う用途を列挙する
		std::span<const AssetDocumentSaveKind> GetSaveKinds();

		// 保存と復旧で同じ対象範囲を使う
		JsonFileJournal::Scope MakeScope(AssetDocumentSaveKind kind);
		// 未完了保存を復旧し、競合した操作を診断へ残す
		bool RecoverPending(std::vector<AssetDatabaseIssue>& issues);
		// 明示選択した操作の現在のファイルを維持する
		bool KeepCurrent(const std::filesystem::path& directory, std::string& diagnostic);
	}
}
