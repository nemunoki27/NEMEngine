#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/MeshNode.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

namespace Engine {

	//============================================================================
	//	SkeletonBuilder functions
	//============================================================================
	// MeshNode階層からSkeletonを構築する、jointMapも合わせて作る
	Skeleton BuildSkeletonFromMeshNode(const MeshNode& rootNode);
} // Engine
