#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>

// c++
#include <vector>

namespace Engine {

	struct Matrix4x4;
	struct TransformComponent;

	//============================================================================
	//	JointAttachmentSystem class
	//	ジョイントに接続したEntityと子孫を更新する
	//============================================================================
	class JointAttachmentSystem : public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		JointAttachmentSystem() = default;
		~JointAttachmentSystem() = default;

		// スケルトン更新後の行列へEntityを追従させる
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		// システムの表示名を取得する
		const char* GetName() const override { return "JointAttachmentSystem"; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// サブツリー更新のためのスタック
		std::vector<Entity> stack_;
		// 変換を更新した描画と照明の通知先
		std::vector<Entity> changedTransforms_;

		//--------- functions ----------------------------------------------------

		// 行列が変わった対象を通知へまとめる
		bool UpdateWorldMatrix(ECSWorld& world, Entity entity, TransformComponent& transform, const Matrix4x4& matrix,
			ComponentChangeChannel& changedChannels);
	};
} // Engine
