#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <string_view>

// front
struct aiBone;
struct aiScene;

namespace Engine {

	//============================================================================
	//	SkeletonBuilder functions
	//============================================================================
	// スキンボーンと必要な親ノードからスケルトンを構築する
	Skeleton BuildSkinSkeleton(const aiScene* scene, std::string_view sourcePath);
	// 表示名または内部ノードパスからジョイントを検索する
	int32_t FindSkeletonJointIndex(const Skeleton& skeleton, std::string_view jointReference);
	// Assimpのボーン実体からジョイントを検索する
	int32_t FindSkeletonJointIndex(const Skeleton& skeleton, const aiBone* bone);
} // Engine
