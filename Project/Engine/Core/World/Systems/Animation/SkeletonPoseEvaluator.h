#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

namespace Engine::SkeletonPoseEvaluator {

	void ApplyClipToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& jointTracks, float time);

	void BlendClipsToSkeleton(Skeleton& skeleton, const std::vector<const NodeAnimation*>& fromTracks, float fromTime,
		const std::vector<const NodeAnimation*>& toTracks, float toTime, float alpha);

	void UpdateSkeletonHierarchy(Skeleton& skeleton);

	void BuildPalette(const Skeleton& skeleton, const SkinCluster& skinCluster, std::vector<WellForGPU>& outPalette);
}
