#include "BuiltinEditorTools.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ToolRegistry.h>
#include <Engine/Editor/Tools/AnimationCurveTool.h>
#include <Engine/Editor/Tools/CameraManagerTool.h>
#include <Engine/Editor/Tools/CollisionManagerTool.h>
#include <Engine/Core/Graphics/Render/View/SceneViewCameraController.h>

// c++
#include <memory>
#include <string>

//============================================================================
//	BuiltinEditorTools functions
//============================================================================

namespace {

	// 既に登録済みならそのまま使い、未登録の場合だけ追加する
	template <typename T>
	void RegisterBuiltinEditorTool() {

		auto tool = std::make_unique<T>();
		const std::string id = tool->GetDescriptor().id;
		if (Engine::ToolRegistry::GetInstance().Find(id)) {
			return;
		}

		Engine::ToolRegistry::GetInstance().Register(std::move(tool));
	}
}

void Engine::RegisterBuiltinEditorTools() {

	RegisterBuiltinEditorTool<CameraManagerTool>();
	RegisterBuiltinEditorTool<SceneViewCameraController>();
	RegisterBuiltinEditorTool<CollisionManagerTool>();
	RegisterBuiltinEditorTool<AnimationCurveTool>();
}
