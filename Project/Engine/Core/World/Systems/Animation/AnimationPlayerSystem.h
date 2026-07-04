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
		//	private variables
		//============================================================================

		// Editプレビュー中のbase値、inspectorのcommitでcomponent runtimeが消えるためsystem側でUUID単位に保持する
		std::unordered_map<UUID, std::vector<AnimationPreviewBaseValue>> editPreviewBase_;

		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 指定名のグループを返す、無ければnullptr
		const AnimationGroup* FindGroup(const AnimationPlayerComponent& player, const std::string& name) const;
		// 全グループの全クリップが触るプロパティのbase値を現在値で捕捉する
		void CaptureBaseValues(ECSWorld& world, const Entity& entity, const AnimationPlayerComponent& player,
			SystemContext& context, std::vector<AnimationPreviewBaseValue>& out) const;
		// 捕捉済みbase値を書き戻す
		void RestoreBaseValues(ECSWorld& world, const Entity& entity,
			const std::vector<AnimationPreviewBaseValue>& base) const;
		// 指定グループへ即時またはクロスフェードで切り替える、グループ内クリップを初期化する
		void BeginGroup(AnimationPlayerComponent& player, const std::string& groupName, float fadeDuration) const;
		// グループ内クリップ1つ分の開始遅延/時間/回数を進める、Play中はこのフレームで跨いだイベントをfiredOutへ集める
		void AdvanceClip(AnimationPlayerComponent& player, const AnimationGroup& group,
			AnimationClipRuntime& clipRt, SystemContext& context, std::vector<AnimationEvent>* firedOut) const;
		// 進める前後の状態から、本編再生フェーズで跨いだイベントをfiredOutへ集める
		void CollectClipEvents(const AnimationClipAsset& clip, AnimationWrapMode wrap,
			float beforeTime, int8_t beforeDir, AnimationClipPhase beforePhase, int32_t beforeRepeat,
			const AnimationClipRuntime& clipRt, float dur, std::vector<AnimationEvent>& firedOut) const;
		// グループ内の開始済みクリップを評価しoutValuesへ入れる、競合プロパティは除外する
		void EvaluateGroupClips(ECSWorld& world, const Entity& entity,
			const AnimationGroup& group, const std::vector<AnimationClipRuntime>& clips,
			std::span<const AnimationPreviewBaseValue> baseStore, SystemContext& context,
			std::vector<AnimationEvaluatedValue>& outValues) const;
		// 1エンティティ分の再生を進めてComponentへ適用する、Editプレビューのbase保持を更新する
		void UpdatePlayer(ECSWorld& world, const Entity& entity,
			AnimationPlayerComponent& player, SystemContext& context);
	};
} // Engine
