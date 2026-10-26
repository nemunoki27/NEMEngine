#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>

namespace Engine::SceneEntityReferenceFields {

	// 保存用の参照をHierarchyのEntityへ対応付けて編集する
	ValueEditResult Draw(const char* label, ECSWorld& world, Entity owner, SceneEntityReference& reference);
}
