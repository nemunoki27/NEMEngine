#include "SystemContext.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

//============================================================================
//	SystemContext structMethods
//============================================================================
void Engine::SystemContext::SetActiveSceneHeader(const SceneHeader* header) {

	// コピーに成功してから現在のScene情報を替える
	activeSceneHeader_ = header ? std::make_shared<const SceneHeader>(*header) : nullptr;
}
