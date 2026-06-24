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
	class HierarchySystem;

	//============================================================================
	//	JointAttachmentUtility namespace
	//	エンティティとスキンメッシュのジョイントの親子付け操作
	//============================================================================
	namespace JointAttachmentUtility {

		// ジョイントのワールド行列を取得する、スケルトン空間行列にスキンメッシュのワールド行列を掛ける
		bool GetJointWorldMatrix(ECSWorld& world, const Entity& skinnedEntity, const std::string& jointName,
			Matrix4x4& outWorldMatrix);

		// 親子付けされたエンティティから、追従先ジョイントのワールド行列を取得する
		bool GetAttachedJointWorldMatrix(ECSWorld& world, const Entity& entity, Matrix4x4& outWorldMatrix);

		// エンティティをジョイントへ親子付けする、ローカルをリセットしてジョイント原点へ合わせる
		void Attach(ECSWorld& world, HierarchySystem& hierarchySystem, const Entity& entity,
			const Entity& skinnedEntity, const std::string& jointName);

		// ジョイント親子付けを解除し、ワールド位置を維持したままルートへ戻す
		void Detach(ECSWorld& world, const Entity& entity);
	}
} // Engine
