#pragma once

//============================================================================
//	include
//============================================================================
#include "UIVisualSession.h"
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <array>
#include <unordered_map>
#include <vector>

namespace Engine {

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
		// 編集中の入力遷移を元の表示へ戻す
		void RestoreEditModeVisuals(ECSWorld& world);

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "UIInputSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		UIVisualSession visuals_;
	};
} // Engine
