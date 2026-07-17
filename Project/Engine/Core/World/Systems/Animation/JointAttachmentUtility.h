#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <string>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	JointAttachmentUtility namespace
	//	ジョイント追従のワールド行列取得、Edit/Play/スクリプトから共通で使う
	//============================================================================
	namespace JointAttachmentUtility {

		// 親子付けされたエンティティから、追従先エンティティとジョイントのスケルトン空間行列を取得する
		bool ResolveAttachedJoint(ECSWorld& world, const Entity& entity, Entity& outSkinnedEntity,
			Matrix4x4& outSkeletonSpaceMatrix);

		// ジョイントのワールド行列を取得する、スケルトン空間行列にスキンメッシュのワールド行列を掛ける
		bool GetJointWorldMatrix(ECSWorld& world, const Entity& skinnedEntity, const std::string& jointName,
			Matrix4x4& outWorldMatrix);

		// 親子付けされたエンティティから、追従先ジョイントのワールド行列を取得する
		bool GetAttachedJointWorldMatrix(ECSWorld& world, const Entity& entity, Matrix4x4& outWorldMatrix);
	}
} // Engine
