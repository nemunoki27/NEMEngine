#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/EditorEntitySnapshot.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cctype>

namespace Engine::EditorEntityDuplicateUtility {

	//============================================================================
	//	EditorEntityDuplicateUtility namespace
	//============================================================================

	// ルート名からEntity_N形式の一意名を作る
	std::string MakeUniqueDuplicatedName(ECSWorld& world, const std::string_view& sourceName);

	// クリップボードへ入れるためにルートの親参照を切る
	void ClearRootParentLink(EditorEntityTreeSnapshot& snapshot);

	// 元スナップショットから複製用のスナップショットを作る
	void BuildDuplicateSnapshot(const EditorEntityTreeSnapshot& sourceSnapshot,
		const std::string_view& duplicatedRootName, EditorEntityTreeSnapshot& outSnapshot);

	// 準備したスナップショットからインスタンスを生成する
	Entity InstantiatePreparedSnapshot(ECSWorld& world, const EditorEntityTreeSnapshot& preparedSnapshot,
		UUID externalParentStableUUID = UUID{});
} // Engine