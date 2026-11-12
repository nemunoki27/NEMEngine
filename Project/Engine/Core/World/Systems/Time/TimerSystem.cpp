#include "TimerSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Time/TimerComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

void Engine::TimerSystem::Update(ECSWorld& world, SystemContext& context) {

	world.ForEach<TimerComponent>([&](Entity owner, TimerComponent& component) {
		if (!component.enabled || !IsEntityActiveInHierarchy(world, owner)) return;
		if (context.mode == WorldMode::Edit && !component.playInEditMode) return;
		const float scaled = context.mode == WorldMode::Edit ? context.unscaledDeltaTime : context.deltaTime;
		const auto time = ResolveLocalTime(world, owner, component.timeSource, TimeChannel::Timer,
			scaled, context.unscaledDeltaTime);
		UpdateTimer(component, time.valid ? time.deltaTime : 0.0);
		if (!time.valid) component.runtimeDiagnostic = "時間源のオブジェクトまたはTimeScaleが見つかりません";
	});
}
