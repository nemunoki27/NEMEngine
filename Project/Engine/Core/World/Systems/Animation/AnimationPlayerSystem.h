#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	// front
	struct AnimationPlayerComponent;
	struct AnimationState;

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
		//	private variables
		//============================================================================

		// Editプレビュー中のbase値、inspectorのcommitでcomponent runtimeが消えるためsystem側でUUID単位に保持する
		std::unordered_map<UUID, std::vector<AnimationPreviewBaseValue>> editPreviewBase_;

		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 指定state名のstateを返す、無ければnullptr
		const AnimationState* FindState(const AnimationPlayerComponent& player, const std::string& name) const;
		// 全stateのプロパティのbase値を現在値で捕捉する
		void CaptureBaseValues(ECSWorld& world, const Entity& entity, const AnimationPlayerComponent& player,
			SystemContext& context, std::vector<AnimationPreviewBaseValue>& out) const;
		// 捕捉済みbase値を書き戻す
		void RestoreBaseValues(ECSWorld& world, const Entity& entity,
			const std::vector<AnimationPreviewBaseValue>& base) const;
		// 指定stateへ即時またはクロスフェードで切り替える
		void BeginState(AnimationPlayerComponent& player, const std::string& stateName, float fadeDuration) const;
		// 1エンティティ分の再生を進めてComponentへ適用する、Editプレビューのbase保持を更新する
		void UpdatePlayer(ECSWorld& world, const Entity& entity,
			AnimationPlayerComponent& player, SystemContext& context);
	};
} // Engine
