#include "UISelectableStyle.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Audio/AudioSystem.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace Engine::UISelectableStyle {

	Engine::MaterialParameterSet* ResolveMaterialParameters( Engine::ECSWorld& world, Engine::Entity target) {

		if (auto* sprite = world.TryGetComponent<Engine::SpriteRendererComponent>(target)) {
			return &sprite->materialInstance;
		}
		if (auto* text = world.TryGetComponent<Engine::TextRendererComponent>(target)) {
			return &text->materialInstance;
		}
		return nullptr;
	}

	bool SetMaterialParameter(Engine::MaterialParameterSet& parameters,
		Engine::MaterialParameterID id, std::string_view name, Engine::MaterialParameterSemantic semantic,
		const Engine::MaterialParameterValue& value) {

		const Engine::MaterialParameterSet& readOnly = parameters;
		const Engine::MaterialParameterValue* current = readOnly.Find(id);
		if (current && current->value == value.value) {
			return false;
		}
		parameters.Set(id, name, semantic, value);
		return true;
	}

	void NotifyRendererMaterialModified(Engine::ECSWorld& world) {

		// UI遷移は保存対象の編集ではないため描画キャッシュだけを更新する
		world.MarkRenderDataModified();
	}

	const Engine::UITransitionStyle& ResolveStyle( const Engine::UISelectableComponent& selectable,
		const Engine::UISelectableRuntimeComponent& runtime) {

		switch (runtime.state) {
		case Engine::UISelectableState::Selected: return selectable.selected;
		case Engine::UISelectableState::Submitted: return selectable.submitted;
		case Engine::UISelectableState::Disabled: return selectable.disabled;
		case Engine::UISelectableState::Normal:
		default: return selectable.normal;
		}
	}

	size_t GetStyleIndex(Engine::UISelectableState state) {

		switch (state) {
		case Engine::UISelectableState::Selected: return 1;
		case Engine::UISelectableState::Submitted: return 2;
		case Engine::UISelectableState::Disabled: return 3;
		case Engine::UISelectableState::Normal:
		default: return 0;
		}
	}

	std::array<const Engine::UITransitionStyle*, 4> GetStyles( const Engine::UISelectableComponent& selectable) {

		return { &selectable.normal,&selectable.selected,&selectable.submitted,&selectable.disabled };
	}

	bool HasStateTransitionRuntime(const Engine::UISelectableComponent& selectable) {

		for (const Engine::UITransitionStyle* style : GetStyles(selectable)) {
			if ((style->animationEnabled && style->useAnimationClip) || style->sound) {
				return true;
			}
		}
		return false;
	}

	void ApplySelectableBaseVisual(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableRuntimeComponent& runtime) {

		if (!runtime.initialized) {
			return;
		}
		if (auto* parameters = ResolveMaterialParameters(world, entity)) {
			bool materialChanged = false;
			if (runtime.hadBaseColor) {
				Engine::MaterialParameterValue value{};
				value.value = runtime.baseColor;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColor,
					Engine::MaterialParameterNames::BaseColor,
					Engine::MaterialParameterSemantic::BaseColor,
					value);
			} else {
				materialChanged |= parameters->erase(
					Engine::MaterialParameterIDs::BaseColor) != 0;
			}
			if (runtime.hadBaseTexture) {
				Engine::MaterialParameterValue value{};
				value.value = runtime.baseTexture;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColorTexture,
					Engine::MaterialParameterNames::BaseColorTexture,
					Engine::MaterialParameterSemantic::BaseColorTexture,
					value);
			} else {
				materialChanged |= parameters->erase(
					Engine::MaterialParameterIDs::BaseColorTexture) != 0;
			}
			if (materialChanged) {
				NotifyRendererMaterialModified(world);
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			if (transform->localScale != runtime.baseScale) {
				transform->localScale = runtime.baseScale;
				Engine::MarkTransformSubtreeDirty(world, entity);
			}
		}
		runtime.currentColor = runtime.baseColor;
		runtime.startColor = runtime.baseColor;
		runtime.currentScale = runtime.baseScale;
		runtime.startScale = runtime.baseScale;
	}

	void RestoreSelectableVisual(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::UISelectableRuntimeComponent& runtime) {

		if (!runtime.initialized) {
			return;
		}
		ApplySelectableBaseVisual(world, entity, runtime);

		// Canvas入力対象から外れた時点で次回初期化用の状態へ戻す
		runtime = {};
	}

	void ResetStateThisFrame(Engine::UISelectableRuntimeComponent& runtime) {

		runtime.normalThisFrame = false;
		runtime.selectedThisFrame = false;
		runtime.submittedThisFrame = false;
		runtime.disabledThisFrame = false;
	}

	void SetStateThisFrame(Engine::UISelectableRuntimeComponent& runtime, Engine::UISelectableState state) {

		switch (state) {
		case Engine::UISelectableState::Normal:
			runtime.normalThisFrame = true;
			break;
		case Engine::UISelectableState::Selected:
			runtime.selectedThisFrame = true;
			break;
		case Engine::UISelectableState::Submitted:
			runtime.submittedThisFrame = true;
			break;
		case Engine::UISelectableState::Disabled:
			runtime.disabledThisFrame = true;
			break;
		}
	}

	void InitializeSelectableRuntime(Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::UISelectableComponent& selectable, Engine::UISelectableRuntimeComponent& runtime) {

		if (runtime.initialized) {
			return;
		}

		runtime.baseColor = Engine::Color4::White();
		runtime.baseTexture = {};
		runtime.hadBaseColor = false;
		runtime.hadBaseTexture = false;
		if (auto* parameters = ResolveMaterialParameters(world, entity)) {
			if (const Engine::MaterialParameterValue* value =
				parameters->Find( Engine::MaterialParameterIDs::BaseColor)) {

					if (const auto* color =
						std::get_if<Engine::Color4>(
							&value->value)) {
						runtime.baseColor = *color;
						runtime.hadBaseColor = true;
					}
				}
			if (const Engine::MaterialParameterValue* value =
				parameters->Find(Engine::MaterialParameterIDs::BaseColorTexture)) {

					if (const auto* texture =
						std::get_if<Engine::AssetID>(
							&value->value)) {
						runtime.baseTexture = *texture;
						runtime.hadBaseTexture = true;
					}
				}
		}
		runtime.baseScale = Engine::Vector3::AnyInit(1.0f);
		if (const auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity)) {
			runtime.baseScale = transform->localScale;
		}
		runtime.currentColor = runtime.baseColor;
		runtime.startColor = runtime.baseColor;
		runtime.currentScale = runtime.baseScale;
		runtime.startScale = runtime.baseScale;
		runtime.previousState = runtime.state;
		const Engine::UITransitionStyle& style =
			ResolveStyle(selectable, runtime);
		runtime.colorTransitionElapsed = style.colorTransitionDuration;
		runtime.scaleTransitionElapsed = style.scaleTransitionDuration;
		runtime.initialized = true;
	}

	void UpdateSelectableVisual(Engine::ECSWorld& world, const UISelectableEntry& entry, float deltaTime) {

		Engine::UISelectableComponent& selectable = *entry.selectable;
		Engine::UISelectableRuntimeComponent& runtime = *entry.runtime;
		InitializeSelectableRuntime(
			world, entry.entity, selectable, runtime);
		if (!runtime.initialized) {
			return;
		}

		if (runtime.previousState != runtime.state) {
			runtime.previousState = runtime.state;
			runtime.colorTransitionElapsed = 0.0f;
			runtime.scaleTransitionElapsed = 0.0f;
			runtime.startColor = runtime.currentColor;
			runtime.startScale = runtime.currentScale;
		}
		const Engine::UITransitionStyle& style =
			ResolveStyle(selectable, runtime);
		if (!style.animationEnabled) {
			ApplySelectableBaseVisual(world, entry.entity, runtime);
			if (style.overrideTexture) {
				if (auto* parameters = ResolveMaterialParameters(world, entry.entity)) {
					Engine::MaterialParameterValue value{};
					value.value = style.texture;
					if (SetMaterialParameter(*parameters,
						Engine::MaterialParameterIDs::BaseColorTexture,
						Engine::MaterialParameterNames::BaseColorTexture,
						Engine::MaterialParameterSemantic::BaseColorTexture,
						value)) {
						NotifyRendererMaterialModified(world);
					}
				}
			}
			return;
		}
		if (style.useAnimationClip) {
			if (style.overrideTexture) {
				if (auto* parameters = ResolveMaterialParameters(world, entry.entity)) {
					Engine::MaterialParameterValue value{};
					value.value = style.texture;
					if (SetMaterialParameter(*parameters,
						Engine::MaterialParameterIDs::BaseColorTexture,
						Engine::MaterialParameterNames::BaseColorTexture,
						Engine::MaterialParameterSemantic::BaseColorTexture,
						value)) {
						NotifyRendererMaterialModified(world);
					}
				}
			}
			return;
		}
		const float elapsedTime = (std::max)(deltaTime, 0.0f);
		runtime.colorTransitionElapsed += elapsedTime;
		runtime.scaleTransitionElapsed += elapsedTime;
		const float colorProgress = style.colorTransitionDuration <= 0.0f ? 1.0f :
			std::clamp(runtime.colorTransitionElapsed / style.colorTransitionDuration, 0.0f, 1.0f);
		const float scaleProgress = style.scaleTransitionDuration <= 0.0f ? 1.0f :
			std::clamp(runtime.scaleTransitionElapsed / style.scaleTransitionDuration, 0.0f, 1.0f);
		const Engine::Color4 targetColor = runtime.baseColor * style.color;
		const Engine::Vector3 targetScale( runtime.baseScale.x * style.scale.x, runtime.baseScale.y * style.scale.y,
			runtime.baseScale.z);
		runtime.currentColor = Engine::Color4::Lerp(
			runtime.startColor, targetColor,
			EasedValue(style.colorEasing, colorProgress));
		runtime.currentScale = Engine::Vector3::Lerp(
			runtime.startScale, targetScale,
			EasedValue(style.scaleEasing, scaleProgress));

		if (auto* parameters = ResolveMaterialParameters(world, entry.entity)) {
			bool materialChanged = false;
			Engine::MaterialParameterValue color{};
			color.value = runtime.currentColor;
			materialChanged |= SetMaterialParameter(*parameters,
				Engine::MaterialParameterIDs::BaseColor,
				Engine::MaterialParameterNames::BaseColor,
				Engine::MaterialParameterSemantic::BaseColor,
				color);
			if (style.overrideTexture) {
				Engine::MaterialParameterValue texture{};
				texture.value = style.texture;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColorTexture,
					Engine::MaterialParameterNames::BaseColorTexture,
					Engine::MaterialParameterSemantic::BaseColorTexture,
					texture);
			} else if (runtime.hadBaseTexture) {
				Engine::MaterialParameterValue texture{};
				texture.value = runtime.baseTexture;
				materialChanged |= SetMaterialParameter(*parameters,
					Engine::MaterialParameterIDs::BaseColorTexture,
					Engine::MaterialParameterNames::BaseColorTexture,
					Engine::MaterialParameterSemantic::BaseColorTexture,
					texture);
			} else {
				materialChanged |= parameters->erase(
					Engine::MaterialParameterIDs::BaseColorTexture) != 0;
			}
			if (materialChanged) {
				NotifyRendererMaterialModified(world);
			}
		}
		if (auto* transform = world.TryGetComponent<Engine::TransformComponent>(entry.entity)) {
			if (transform->localScale != runtime.currentScale) {
				transform->localScale = runtime.currentScale;
				Engine::MarkTransformSubtreeDirty(world, entry.entity);
			}
		}
	}
}
