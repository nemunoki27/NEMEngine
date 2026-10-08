#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>

namespace Engine::SceneCompositionOperations {

	// 構成案を検証して反映し失敗時は以前の構成へ戻す
	bool ApplyChanges(const EditorToolContext& context, SceneInstance& instance,
		const std::vector<SubSceneSlotDesc>& proposed, std::string& statusMessage, bool& statusError);
}
