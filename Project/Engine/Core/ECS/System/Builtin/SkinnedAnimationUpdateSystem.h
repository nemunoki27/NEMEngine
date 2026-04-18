#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Interface/ISystem.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Graphics/Mesh/Animation/SkinnedMeshAnimationManager.h>

// c++
#include <execution>

namespace Engine {

	//============================================================================
	//	SkinnedAnimationUpdateSystem class
	//	骨アニメーションデータの更新を行うシステム
	//============================================================================
	class SkinnedAnimationUpdateSystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SkinnedAnimationUpdateSystem() = default;
		~SkinnedAnimationUpdateSystem() = default;

		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "SkinnedAnimationUpdateSystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// Vector3/Quaternionで特殊化
		template <typename TValue>
		TValue SampleKeyframes(const std::vector<Keyframe<TValue>>& keys, float time);

		// アニメーションの適用
		void ApplyClipToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& jointTracks, float time);
		// アニメーションのブレンド
		void BlendClipsToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& fromTracks,
			float fromTime, const std::vector<const NodeAnimation*>& toTracks, float toTime, float alpha);

		// スケルトンの階層を更新
		void UpdateSkeletonHierarchy(Skeleton& skeleton);
		// GPU用のパレットを構築
		void BuildPalette(const Skeleton& skeleton, const SkinCluster& skinCluster, std::vector<WellForGPU>& outPalette);

		// 初期クリップの名前を解決
		std::string ResolveInitialClip(const SkinnedMeshAnimationSet& animationSet, const std::string& requestedClip);
	};
} // Engine