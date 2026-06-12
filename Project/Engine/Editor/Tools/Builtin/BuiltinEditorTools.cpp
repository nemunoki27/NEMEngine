#include "BuiltinEditorTools.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/Registry/ToolRegistry.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipTool.h>
#include <Engine/Editor/Tools/Builtin/Camera/CameraManagerTool.h>
#include <Engine/Editor/Tools/Builtin/Collision/CollisionManagerTool.h>
#include <Engine/Editor/Tools/Builtin/PostProcess/PostProcessStackTool.h>
#include <Engine/Editor/Tools/Builtin/Scripting/ScriptExecutionOrderTool.h>
#include <Engine/Editor/Tools/Builtin/Scripting/ScriptBuildDiagnosticsTool.h>
#include <Engine/Editor/Tools/Builtin/Scripting/ScriptExceptionListTool.h>
#include <Engine/Editor/Tools/Builtin/Scripting/ScriptProfilerTool.h>
#include <Engine/Core/Rendering/Renderer/Views/SceneViewCameraController.h>

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
	RegisterBuiltinEditorTool<AnimationClipTool>();
	RegisterBuiltinEditorTool<PostProcessStackTool>();
	RegisterBuiltinEditorTool<ScriptExecutionOrderTool>();
	RegisterBuiltinEditorTool<ScriptBuildDiagnosticsTool>();
	RegisterBuiltinEditorTool<ScriptExceptionListTool>();
	RegisterBuiltinEditorTool<ScriptProfilerTool>();
}
