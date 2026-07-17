#include "UICanvasSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

//============================================================================
//	UICanvasSystem internal
//============================================================================
namespace {

	Engine::UUID GetLocalFileID(Engine::ECSWorld& world, Engine::Entity entity) {

		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
		return sceneObject ? sceneObject->localFileID : Engine::UUID{};
	}

	Engine::Entity ResolveTarget(Engine::ECSWorld& world, Engine::UUID localFileID) {

		if (!localFileID) {
			return Engine::Entity::Null();
		}
		const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(world, localFileID);
		return world.IsAlive(target) ? target : Engine::Entity::Null();
	}

	void SyncDelayedRenderer(Engine::ECSWorld& world, Engine::Entity source,
		Engine::Entity delayedTarget, bool syncGeometry) {

		const auto* sourceSprite = world.TryGetComponent<Engine::SpriteRendererComponent>(source);
		auto* delayedSprite = world.TryGetComponent<Engine::SpriteRendererComponent>(delayedTarget);
		if (!sourceSprite || !delayedSprite) {
			return;
		}

		delayedSprite->material = sourceSprite->material;
		delayedSprite->parameterOverrides = sourceSprite->parameterOverrides;
		delayedSprite->layer = sourceSprite->layer;
		delayedSprite->order = sourceSprite->order;
		if ((std::numeric_limits<int32_t>::min)() < delayedSprite->order) {
			--delayedSprite->order;
		}
		delayedSprite->visible = sourceSprite->visible;
		delayedSprite->blendMode = sourceSprite->blendMode;
		delayedSprite->queue = sourceSprite->queue;
		if (!syncGeometry) {
			return;
		}

		delayedSprite->size = sourceSprite->size;
		delayedSprite->pivot = sourceSprite->pivot;
		const auto* sourceUVTransform = world.TryGetComponent<Engine::UVTransformComponent>(source);
		auto* delayedUVTransform = world.TryGetComponent<Engine::UVTransformComponent>(delayedTarget);
		if (sourceUVTransform && delayedUVTransform) {
			*delayedUVTransform = *sourceUVTransform;
		}
	}

	void RestoreTarget(Engine::ECSWorld& world, const Engine::UIProgressTargetRuntime& runtime) {

		if (!runtime.valid || !runtime.localFileID) {
			return;
		}
		const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(world, runtime.localFileID);
		if (!world.IsAlive(target)) {
			return;
		}
		if (auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(target)) {
			sprite->size = runtime.size;
			sprite->pivot = runtime.pivot;
		}
		if (runtime.hasUVTransform) {
			if (auto* uvTransform = world.TryGetComponent<Engine::UVTransformComponent>(target)) {
				uvTransform->pos = runtime.uvPos;
				uvTransform->scale = runtime.uvScale;
			}
		}
	}

	void EnsureTarget(Engine::ECSWorld& world, Engine::Entity target,
		Engine::UIProgressTargetRuntime& runtime) {

		const Engine::UUID targetLocalFileID = world.IsAlive(target) ? GetLocalFileID(world, target) : Engine::UUID{};
		if (runtime.valid && runtime.localFileID == targetLocalFileID) {
			return;
		}

		RestoreTarget(world, runtime);
		runtime = {};
		if (!targetLocalFileID) {
			return;
		}
		const auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(target);
		if (!sprite) {
			return;
		}
		runtime.localFileID = targetLocalFileID;
		runtime.size = sprite->size;
		runtime.pivot = sprite->pivot;
		if (const auto* uvTransform = world.TryGetComponent<Engine::UVTransformComponent>(target)) {
			runtime.hasUVTransform = true;
			runtime.uvPos = uvTransform->pos;
			runtime.uvScale = uvTransform->scale;
		}
		runtime.valid = true;
	}

	void ApplyFill(Engine::ECSWorld& world, const Engine::UIProgressTargetRuntime& runtime,
		Engine::UIProgressFillDirection direction, float ratio) {

		if (!runtime.valid || !runtime.localFileID) {
			return;
		}
		const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(world, runtime.localFileID);
		auto* sprite = world.IsAlive(target) ? world.TryGetComponent<Engine::SpriteRendererComponent>(target) : nullptr;
		if (!sprite) {
			return;
		}

		ratio = std::clamp(ratio, 0.0f, 1.0f);
		sprite->size = runtime.size;
		sprite->pivot = runtime.pivot;
		switch (direction) {
		case Engine::UIProgressFillDirection::LeftToRight:
			sprite->size.x *= ratio;
			if (0.0f < ratio) { sprite->pivot.x = runtime.pivot.x / ratio; }
			break;
		case Engine::UIProgressFillDirection::RightToLeft:
			sprite->size.x *= ratio;
			if (0.0f < ratio) { sprite->pivot.x = 1.0f - (1.0f - runtime.pivot.x) / ratio; }
			break;
		case Engine::UIProgressFillDirection::TopToBottom:
			sprite->size.y *= ratio;
			if (0.0f < ratio) { sprite->pivot.y = runtime.pivot.y / ratio; }
			break;
		case Engine::UIProgressFillDirection::BottomToTop:
			sprite->size.y *= ratio;
			if (0.0f < ratio) { sprite->pivot.y = 1.0f - (1.0f - runtime.pivot.y) / ratio; }
			break;
		}

		if (!runtime.hasUVTransform) {
			return;
		}
		auto* uvTransform = world.TryGetComponent<Engine::UVTransformComponent>(target);
		if (!uvTransform) {
			return;
		}
		uvTransform->pos = runtime.uvPos;
		uvTransform->scale = runtime.uvScale;
		switch (direction) {
		case Engine::UIProgressFillDirection::LeftToRight:
			uvTransform->scale.x = runtime.uvScale.x * ratio;
			break;
		case Engine::UIProgressFillDirection::RightToLeft:
			uvTransform->scale.x = runtime.uvScale.x * ratio;
			uvTransform->pos.x = runtime.uvPos.x + runtime.uvScale.x * (1.0f - ratio);
			break;
		case Engine::UIProgressFillDirection::TopToBottom:
			uvTransform->scale.y = runtime.uvScale.y * ratio;
			break;
		case Engine::UIProgressFillDirection::BottomToTop:
			uvTransform->scale.y = runtime.uvScale.y * ratio;
			uvTransform->pos.y = runtime.uvPos.y + runtime.uvScale.y * (1.0f - ratio);
			break;
		}
	}

	float NormalizeValue(const Engine::UIProgressComponent& progress) {

		const float range = progress.maxValue - progress.minValue;
		if (std::abs(range) <= 0.00001f) {
			return 0.0f;
		}
		return std::clamp((progress.value - progress.minValue) / range, 0.0f, 1.0f);
	}

	void UpdateProgress(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UIProgressComponent& progress, float deltaTime) {

		if (!progress.enabled) {
			RestoreTarget(world, progress.runtimeFillTarget);
			RestoreTarget(world, progress.runtimeDelayedTarget);
			progress.runtimeInitialized = false;
			return;
		}

		EnsureTarget(world, entity, progress.runtimeFillTarget);
		if (progress.delayed && progress.delayedTargetLocalFileID) {

			const Engine::Entity delayedTarget =
				ResolveTarget(world, progress.delayedTargetLocalFileID);
			SyncDelayedRenderer(world, entity, delayedTarget, !progress.runtimeInitialized);
			EnsureTarget(world, delayedTarget, progress.runtimeDelayedTarget);
		} else {
			RestoreTarget(world, progress.runtimeDelayedTarget);
			progress.runtimeDelayedTarget = {};
		}

		const float target = NormalizeValue(progress);
		if (!progress.runtimeInitialized) {
			progress.runtimeDisplayedValue = target;
			progress.runtimeDelayedValue = target;
			progress.runtimeDisplayStart = target;
			progress.runtimeDelayedStart = target;
			progress.runtimeTargetValue = target;
			progress.runtimeInitialized = true;
		}
		if (target != progress.runtimeTargetValue) {
			progress.runtimeDisplayStart = progress.runtimeDisplayedValue;
			progress.runtimeDelayedStart = progress.runtimeDelayedValue;
			progress.runtimeTargetValue = target;
			progress.runtimeSmoothElapsed = 0.0f;
			progress.runtimeDelayedElapsed = 0.0f;
			if (progress.runtimeDelayedValue < target) {
				progress.runtimeDelayedValue = target;
				progress.runtimeDelayedStart = target;
			}
		}

		if (!progress.smooth || progress.smoothDuration <= 0.0f) {
			progress.runtimeDisplayedValue = target;
		} else {
			progress.runtimeSmoothElapsed += (std::max)(deltaTime, 0.0f);
			const float t = std::clamp(progress.runtimeSmoothElapsed / progress.smoothDuration, 0.0f, 1.0f);
			progress.runtimeDisplayedValue = std::lerp(progress.runtimeDisplayStart,
				target, EasedValue(progress.smoothEasing, t));
		}

		if (!progress.delayed) {
			progress.runtimeDelayedValue = progress.runtimeDisplayedValue;
		} else if (target < progress.runtimeDelayedValue) {
			progress.runtimeDelayedElapsed += (std::max)(deltaTime, 0.0f);
			if (progress.delayedWait < progress.runtimeDelayedElapsed) {
				const float duration = (std::max)(progress.delayedDuration, 0.0001f);
				const float t = std::clamp((progress.runtimeDelayedElapsed - progress.delayedWait) / duration, 0.0f, 1.0f);
				progress.runtimeDelayedValue = std::lerp(progress.runtimeDelayedStart,
					target, EasedValue(progress.delayedEasing, t));
			}
		}

		ApplyFill(world, progress.runtimeFillTarget, progress.direction, progress.runtimeDisplayedValue);
		if (progress.runtimeDelayedTarget.valid &&
			progress.runtimeDelayedTarget.localFileID != progress.runtimeFillTarget.localFileID) {
			ApplyFill(world, progress.runtimeDelayedTarget, progress.direction, progress.runtimeDelayedValue);
		}
	}
}

//============================================================================
//	UICanvasSystem classMethods
//============================================================================
void Engine::UICanvasSystem::Update(ECSWorld& world, SystemContext& context) {

	if (context.mode != WorldMode::Play) {
		return;
	}
	world.ForEach<UIProgressComponent>([&](Entity entity, UIProgressComponent& progress) {

		const float deltaTime = progress.useUnscaledTime ? context.unscaledDeltaTime : context.deltaTime;
		UpdateProgress(world, entity, progress, deltaTime);
		});
}

void Engine::UICanvasSystem::LateUpdate(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UIRuntimeService::GetInstance().Build(world, EngineContext::GetWindowSetting().gameSizeFloat);
}

void Engine::UICanvasSystem::OnWorldExit(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	UIRuntimeService::GetInstance().Clear(world);
}
