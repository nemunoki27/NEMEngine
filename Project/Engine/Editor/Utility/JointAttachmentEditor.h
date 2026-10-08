#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <string>

namespace Engine {

	// 前方宣言
	class ECSWorld;
	class HierarchySystem;

	//============================================================================
	//	JointAttachmentEditor namespace
	//	エンティティとスキンメッシュのジョイントの親子付け操作
	//============================================================================
	namespace JointAttachmentEditor {

		// EntityをJointへ接続して原点へ合わせる
		void Attach(ECSWorld& world, HierarchySystem& hierarchySystem, const Entity& entity, const Entity& skinnedEntity,
			const std::string& jointName);

		// Joint接続を解除してワールド位置を維持する
		void Detach(ECSWorld& world, const Entity& entity);
	}
} // Engine
