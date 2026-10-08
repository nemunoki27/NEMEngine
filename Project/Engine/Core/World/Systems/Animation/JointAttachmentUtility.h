#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <string>

namespace Engine {

	// 前方宣言
	class ECSWorld;

	//============================================================================
	//	JointAttachmentUtility namespace
	//	Joint追従の行列を編集と実行で共通取得する
	//============================================================================
	namespace JointAttachmentUtility {

		// 接続先Entityと骨格空間のJoint行列を取得する
		bool ResolveAttachedJoint(
			const ECSWorld& world, const Entity& entity, Entity& outSkinnedEntity, Matrix4x4& outSkeletonSpaceMatrix);

		// 指定したJointのワールド行列を取得する
		bool GetJointWorldMatrix(
			const ECSWorld& world, const Entity& skinnedEntity, const std::string& jointName, Matrix4x4& outWorldMatrix);

		// Entityが追従するJointのワールド行列を取得する
		bool GetAttachedJointWorldMatrix(const ECSWorld& world, const Entity& entity, Matrix4x4& outWorldMatrix);
	}
} // Engine
