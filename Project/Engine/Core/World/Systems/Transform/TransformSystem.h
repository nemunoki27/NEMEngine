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

		// ワールド接続時に変更通知を購読し初期dirtyを収集する
		void OnWorldEnter(ECSWorld& world, SystemContext& context) override;
		// ワールド切断時に変更通知を解除する
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		// 固定更新中の変更をワールド行列へ反映する
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		// フレーム更新中の変更をワールド行列へ反映する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;
		// シーン追加後の初期dirtyを収集する
		void OnSceneInstancesChanged(
			ECSWorld& world, SystemContext& context,
			SceneChangePhase phase) override;

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
		std::vector<Entity> queuedTransforms_;
		std::vector<Entity> changedTransforms_;
		std::vector<StackNode> stack_;
		uint64_t mutationListenerID_ = 0;
		uint32_t transformTypeID_ = 0;

		// Transform変更通知をdirtyキューへ積む
		static void OnComponentMutation(ECSWorld& world,
			const Entity& entity, uint32_t typeID,
			ComponentMutationKind kind, void* userData);
		// 現在dirtyなTransformを初回、シーン変更時だけ収集する
		void QueueExistingDirtyTransforms(ECSWorld& world);
		// dirty階層の先頭から子孫のワールド行列を更新する
		ComponentChangeChannel UpdateDirtySubtree(
			ECSWorld& world, const Entity& entity);
		// dirtyなトランスフォーム階層を更新する
		void UpdateTransforms(ECSWorld& world);
	};
} // Engine
