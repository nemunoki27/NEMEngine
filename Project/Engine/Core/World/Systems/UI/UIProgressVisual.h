#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::UIProgressVisual {

	// 表示値と遅延表示を更新
	void UpdateProgress(ECSWorld& world, Entity entity, UIProgressComponent& progress, UIProgressRuntimeData& runtime, float deltaTime);
	// 適用前のMaterial値へ復元
	void RestoreProgressVisual(ECSWorld& world, const Entity& entity, UIProgressComponent& progress);
}
