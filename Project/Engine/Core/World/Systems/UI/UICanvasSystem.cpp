#include "UICanvasSystem.h"

//============================================================================
//	include
//============================================================================
#include "UIProgressVisual.h"
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

//============================================================================
//	UICanvasSystem internal
//============================================================================

//============================================================================
//	UICanvasSystem classMethods
//============================================================================
void Engine::UICanvasSystem::RestoreProgressVisual(ECSWorld& world,
	const Entity& entity, UIProgressComponent& progress) {

	UIProgressVisual::RestoreProgressVisual(world, entity, progress);
}

void Engine::UICanvasSystem::Update(ECSWorld& world, SystemContext& context) {

	const bool isPlay = context.mode == WorldMode::Play;
	world.ForEach<UIProgressComponent, UIProgressRuntimeComponent>(
		[&](Entity entity, UIProgressComponent& progress,
			[[maybe_unused]] UIProgressRuntimeComponent& runtimeComponent) {

		UIProgressRuntimeData* runtime = TryGetUIProgressRuntime(world, entity);
		if (!runtime) {
			return;
		}
		if (!IsEntityActiveInHierarchy(world, entity)) {
			return;
		}
		if (!progress.enabled || (!isPlay && !progress.previewInEditMode)) {
			if (runtime->initialized ||
				runtime->fillTarget.valid || runtime->delayedTarget.valid) {
				RestoreProgressVisual(world, entity, progress);
			}
			return;
		}

		const float deltaTime = (!isPlay || progress.useUnscaledTime) ?
			context.unscaledDeltaTime : context.deltaTime;
		UIProgressVisual::UpdateProgress(world, entity, progress, *runtime, deltaTime);
		});
}

void Engine::UICanvasSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UIRuntimeService::GetInstance().Build(world, EngineContext::GetWindowSetting().gameSizeFloat);
}

void Engine::UICanvasSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	world.ForEach<UIProgressComponent, UIProgressRuntimeComponent>(
		[&](Entity entity, UIProgressComponent& progress,
			[[maybe_unused]] UIProgressRuntimeComponent& runtimeComponent) {
			RestoreProgressVisual(world, entity, progress);
			});
	UIRuntimeService::GetInstance().Clear(world);
}
