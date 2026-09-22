#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <array>
#include <vector>

namespace Engine {

	class Input;

	struct UISelectableEntry {

		Entity entity = Entity::Null();
		Entity canvas = Entity::Null();
		UISelectableComponent* selectable = nullptr;
		UISelectableRuntimeComponent* runtime = nullptr;
		const UIElementRuntime* element = nullptr;
		Vector2 center{};
	};

	struct UISelectableAnimationRuntime {

		std::array<AssetID, 4> configuredClips{};
		std::array<bool, 4> configuredAnimations{};
		std::array<bool, 4> configuredUseClips{};
		std::vector<AnimationPreviewBaseValue> baseValues;
		AssetID activeClip{};
		float time = 0.0f;
		uint8_t state = 0;
		bool configured = false;
		bool baseCaptured = false;
		bool stateInitialized = false;
		bool playing = false;
		bool applied = false;
	};
}
