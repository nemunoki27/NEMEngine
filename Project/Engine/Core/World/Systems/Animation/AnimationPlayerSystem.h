#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

// c++
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	// front
	struct AnimationPlayerComponent;
	struct AnimationState;
	struct AnimationGroup;
	struct AnimationClipRuntime;
	enum class AnimationWrapMode : uint8_t;
	enum class AnimationClipPhase : uint8_t;

	//============================================================================
	//	AnimationPlayerSystem class
	//	AnimationClipを名前付きstateで再生しクロスフェードするシステム
	//============================================================================
	class AnimationPlayerSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AnimationPlayerSystem() = default;
		~AnimationPlayerSystem() = default;

		void Update(ECSWorld& world, SystemContext& context) override;
		// World切り替え時にEditプレビューで適用した値を元へ戻して破棄する
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "AnimationPlayerSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// Editプレビュー中のbase値、inspectorのcommitでcomponent runtimeが消えるためsystem側でUUID単位に保持する
		std::unordered_map<UUID, std::vector<AnimationPreviewBaseValue>> editPreviewBase_;

		//--------- functions ----------------------------------------------------

		// 1エンティティ分の再生を進めてComponentへ適用する、Editプレビューのbase保持を更新する
		void UpdatePlayer(ECSWorld& world, const Entity& entity,
			AnimationPlayerComponent& player, SystemContext& context);
	};
} // Engine
