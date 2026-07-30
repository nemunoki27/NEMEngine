#include "UICanvasSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>
#include <cmath>
#include <limits>

//============================================================================
//	UICanvasSystem internal
//============================================================================
namespace {

	constexpr const char* kProgressParameter = "progress";
	constexpr const char* kDirectionParameter = "fillDirection";
	constexpr const char* kPrimitiveTypeParameter = "primitiveType";
	constexpr const char* kBaseColorTextureParameter = "baseColorTexture";

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
		Engine::Entity delayedTarget, Engine::AssetID delayedTexture) {

		const auto* sourcePrimitive = world.TryGetComponent<Engine::PrimitiveRendererComponent>(source);
		auto* delayedPrimitive = world.TryGetComponent<Engine::PrimitiveRendererComponent>(delayedTarget);
		if (!sourcePrimitive || !delayedPrimitive) {
			return;
		}

		*delayedPrimitive = *sourcePrimitive;
		if (delayedTexture) {
			delayedPrimitive->parameterOverrides[kBaseColorTextureParameter].value =
				delayedTexture;
		}
		if ((std::numeric_limits<int32_t>::min)() < delayedPrimitive->order) {
			--delayedPrimitive->order;
		}
	}

	void RestoreParameter(std::unordered_map<std::string, Engine::MaterialParameterValue>& parameters,
		const char* name, bool existed, const Engine::MaterialParameterValue& value) {

		if (existed) {
			parameters[name] = value;
		} else {
			parameters.erase(name);
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
		auto* primitive = world.TryGetComponent<Engine::PrimitiveRendererComponent>(target);
		if (!primitive) {
			return;
		}
		RestoreParameter(primitive->parameterOverrides, kProgressParameter,
			runtime.hadProgressParameter, runtime.progressParameter);
		RestoreParameter(primitive->parameterOverrides, kDirectionParameter,
			runtime.hadDirectionParameter, runtime.directionParameter);
		RestoreParameter(primitive->parameterOverrides, kPrimitiveTypeParameter,
			runtime.hadPrimitiveTypeParameter, runtime.primitiveTypeParameter);
	}

	void CaptureParameter(const std::unordered_map<std::string, Engine::MaterialParameterValue>& parameters,
		const char* name, bool& outExisted, Engine::MaterialParameterValue& outValue) {

		const auto found = parameters.find(name);
		outExisted = found != parameters.end();
		if (outExisted) {
			outValue = found->second;
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
		auto* primitive = world.TryGetComponent<Engine::PrimitiveRendererComponent>(target);
		if (!primitive) {
			return;
		}
		if (!primitive->material ||
			primitive->material == Engine::BuiltinAssets::Materials::DefaultPrimitive2D) {
			primitive->material = Engine::BuiltinAssets::Materials::ProgressPrimitive;
		}
		runtime.localFileID = targetLocalFileID;
		CaptureParameter(primitive->parameterOverrides, kProgressParameter,
			runtime.hadProgressParameter, runtime.progressParameter);
		CaptureParameter(primitive->parameterOverrides, kDirectionParameter,
			runtime.hadDirectionParameter, runtime.directionParameter);
		CaptureParameter(primitive->parameterOverrides, kPrimitiveTypeParameter,
			runtime.hadPrimitiveTypeParameter, runtime.primitiveTypeParameter);
		runtime.valid = true;
	}

	void ApplyFill(Engine::ECSWorld& world, const Engine::UIProgressTargetRuntime& runtime,
		Engine::UIProgressFillDirection direction, float ratio) {

		if (!runtime.valid || !runtime.localFileID) {
			return;
		}
		const Engine::Entity target = Engine::SceneObjectUtility::FindByLocalFileID(world, runtime.localFileID);
		auto* primitive = world.IsAlive(target) ?
			world.TryGetComponent<Engine::PrimitiveRendererComponent>(target) : nullptr;
		if (!primitive) {
			return;
		}

		ratio = std::clamp(ratio, 0.0f, 1.0f);
		primitive->parameterOverrides[kProgressParameter].value = ratio;
		primitive->parameterOverrides[kDirectionParameter].value = static_cast<uint32_t>(direction);
		primitive->parameterOverrides[kPrimitiveTypeParameter].value =
			primitive->type == Engine::PrimitiveType::Ring ? 1u : 0u;
	}

	float NormalizeValue(const Engine::UIProgressComponent& progress) {

		const float range = progress.maxValue - progress.minValue;
		if (std::abs(range) <= 0.00001f) {
			return 0.0f;
		}
		return std::clamp((progress.value - progress.minValue) / range, 0.0f, 1.0f);
	}

	void UpdateProgress(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UIProgressComponent& progress,
		Engine::UIProgressRuntimeData& runtime, float deltaTime) {

		EnsureTarget(world, entity, runtime.fillTarget);
		if (progress.delayed && progress.delayedTargetLocalFileID) {

			const Engine::Entity delayedTarget =
				ResolveTarget(world, progress.delayedTargetLocalFileID);
			SyncDelayedRenderer(world, entity, delayedTarget, progress.delayedTexture);
			EnsureTarget(world, delayedTarget, runtime.delayedTarget);
		} else {
			RestoreTarget(world, runtime.delayedTarget);
			runtime.delayedTarget = {};
		}

		const float target = NormalizeValue(progress);
		if (!runtime.initialized) {
			runtime.displayedValue = target;
			runtime.delayedValue = target;
			runtime.displayStart = target;
			runtime.delayedStart = target;
			runtime.targetValue = target;
			runtime.initialized = true;
		}
		if (target != runtime.targetValue) {
			runtime.displayStart = runtime.displayedValue;
			runtime.delayedStart = runtime.delayedValue;
			runtime.targetValue = target;
			runtime.smoothElapsed = 0.0f;
			runtime.delayedElapsed = 0.0f;
			if (runtime.delayedValue < target) {
				runtime.delayedValue = target;
				runtime.delayedStart = target;
			}
		}

		if (!progress.smooth || progress.smoothDuration <= 0.0f) {
			runtime.displayedValue = target;
		} else {
			runtime.smoothElapsed += (std::max)(deltaTime, 0.0f);
			const float t = std::clamp(runtime.smoothElapsed / progress.smoothDuration, 0.0f, 1.0f);
			runtime.displayedValue = std::lerp(runtime.displayStart,
				target, EasedValue(progress.smoothEasing, t));
		}

		if (!progress.delayed) {
			runtime.delayedValue = runtime.displayedValue;
		} else if (target < runtime.delayedValue) {
			runtime.delayedElapsed += (std::max)(deltaTime, 0.0f);
			if (progress.delayedWait < runtime.delayedElapsed) {
				const float duration = (std::max)(progress.delayedDuration, 0.0001f);
				const float t = std::clamp((runtime.delayedElapsed - progress.delayedWait) / duration, 0.0f, 1.0f);
				runtime.delayedValue = std::lerp(runtime.delayedStart,
					target, EasedValue(progress.delayedEasing, t));
			}
		}

		ApplyFill(world, runtime.fillTarget, progress.direction, runtime.displayedValue);
		if (runtime.delayedTarget.valid &&
			runtime.delayedTarget.localFileID != runtime.fillTarget.localFileID) {
			ApplyFill(world, runtime.delayedTarget, progress.direction, runtime.delayedValue);
		}
	}
}

//============================================================================
//	UICanvasSystem classMethods
//============================================================================
void Engine::UICanvasSystem::RestoreProgressVisual(ECSWorld& world,
	const Entity& entity, UIProgressComponent& progress) {

	UIProgressRuntimeData* runtime = TryGetUIProgressRuntime(world, entity);
	if (!runtime) {
		return;
	}

	// Previewで上書きした描画パラメータを編集前の値へ戻す
	RestoreTarget(world, runtime->fillTarget);
	RestoreTarget(world, runtime->delayedTarget);
	ResetUIProgressRuntime(*runtime, NormalizeValue(progress));
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
		if (!progress.enabled || (!isPlay && !progress.previewInEditMode)) {
			if (runtime->initialized ||
				runtime->fillTarget.valid || runtime->delayedTarget.valid) {
				RestoreProgressVisual(world, entity, progress);
			}
			return;
		}

		const float deltaTime = (!isPlay || progress.useUnscaledTime) ?
			context.unscaledDeltaTime : context.deltaTime;
		UpdateProgress(world, entity, progress, *runtime, deltaTime);
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
