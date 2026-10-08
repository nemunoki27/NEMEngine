#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

namespace Engine::SkeletonPoseEvaluator {

	// Clipの位置と回転とスケールを骨格へ適用する
	void ApplyClipToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& jointTracks, float time);

	// 2つのClipの骨格ポーズを合成する
	void BlendClipsToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& fromTracks, float fromTime,
		const std::vector<const NodeAnimation*>& toTracks, float toTime, float alpha);

	// 親の行列を継承して骨格空間へ変換する
	void UpdateSkeletonHierarchy(Skeleton& skeleton);

	// 骨格と逆バインド行列から描画用パレットを作る
	void BuildPalette(const Skeleton& skeleton, const SkinCluster& skinCluster, std::vector<WellForGPU>& outPalette);
}
