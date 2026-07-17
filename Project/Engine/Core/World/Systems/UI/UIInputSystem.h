#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <array>
#include <unordered_map>
#include <vector>

namespace Engine {

	struct UISelectableAnimationRuntime {

		std::array<AssetID, 4> configuredClips{};
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

	//============================================================================
	//	UIInputSystem class
	//	Canvas配下のナビゲーション入力を処理する
	//============================================================================
	class UIInputSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIInputSystem() = default;
		~UIInputSystem() = default;

		void Update(ECSWorld& world, SystemContext& context) override;
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "UIInputSystem"; }
	private:
		//============================================================================
		//	private variables
		//============================================================================

		// UI状態クリップの再生時間と復元値
		std::unordered_map<UUID, UISelectableAnimationRuntime> animationRuntimes_;
	};
} // Engine
