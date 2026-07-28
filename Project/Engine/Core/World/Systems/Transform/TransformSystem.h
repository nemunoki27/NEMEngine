#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	//============================================================================
	//	TransformSystem class
	//	トランスフォームの更新、管理を行うシステム
	//============================================================================
	class TransformSystem :
		public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TransformSystem() = default;
		~TransformSystem() = default;

		// 固定更新中の変更をワールド行列へ反映する
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		// フレーム更新中の変更をワールド行列へ反映する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "TransformSystem"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// トランスフォーム更新のためのスタックノード
		struct StackNode {

			Entity entity = Entity::Null();
			bool updateWorld = false;
		};

		//--------- variables ----------------------------------------------------

		std::vector<Entity> dirtyTransforms_;
		std::vector<StackNode> stack_;

		// dirty階層の先頭から子孫のワールド行列を更新する
		void UpdateDirtySubtree(ECSWorld& world, const Entity& entity);
		// dirtyなトランスフォーム階層を更新する
		void UpdateTransforms(ECSWorld& world);
	};
} // Engine
