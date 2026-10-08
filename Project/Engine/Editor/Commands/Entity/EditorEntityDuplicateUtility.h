#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <string_view>

namespace Engine::EditorEntityDuplicateUtility {

	//============================================================================
	//	EditorEntityDuplicateUtility namespace
	//============================================================================
	// ルート名からEntity_N形式の一意名を作る
	std::string MakeUniqueDuplicatedName(ECSWorld& world, const std::string_view& sourceName);

	// クリップボードへ入れるためにルートの親参照を切る
	void ClearRootParentLink(EditorEntityTreeSnapshot& snapshot);

	// 準備したスナップショットからインスタンスを生成する
	Entity InstantiatePreparedSnapshot(ECSWorld& world, const EditorEntityTreeSnapshot& preparedSnapshot,
		UUID externalParentStableUUID = UUID{});
} // Engine
