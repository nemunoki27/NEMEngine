#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <string>

namespace Engine {

	// front
	class ECSWorld;
	class HierarchySystem;

	//============================================================================
	//	JointAttachmentEditor namespace
	//	エンティティとスキンメッシュのジョイントの親子付け操作
	//============================================================================
	namespace JointAttachmentEditor {

		// GetJointWorldMatrix / GetAttachedJointWorldMatrix はCore側へ移設済み

		// エンティティをジョイントへ親子付けする、ローカルをリセットしてジョイント原点へ合わせる
		void Attach(ECSWorld& world, HierarchySystem& hierarchySystem, const Entity& entity,
			const Entity& skinnedEntity, const std::string& jointName);

		// ジョイント親子付けを解除し、ワールド位置を維持したままルートへ戻す
		void Detach(ECSWorld& world, const Entity& entity);
	}
} // Engine
