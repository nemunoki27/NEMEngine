#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/World/ECS/World/ECSChangeTracker.h>

// c++
#include <cstdint>
#include <vector>

namespace Engine {

	//============================================================================
	//	TransformSystem class
	//	トランスフォームの更新、管理を行うシステム
	//============================================================================
	class TransformSystem : public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TransformSystem() = default;
		~TransformSystem() = default;

		// World接続時に変更通知を購読し再計算対象を収集する
		void OnWorldEnter(ECSWorld& world, SystemContext& context) override;
		// ワールド切断時に変更通知を解除する
		void OnWorldExit(ECSWorld& world, SystemContext& context) override;
		// 固定更新中の変更をワールド行列へ反映する
		void FixedUpdate(ECSWorld& world, SystemContext& context) override;
		// フレーム更新中の変更をワールド行列へ反映する
		void LateUpdate(ECSWorld& world, SystemContext& context) override;
		// Scene追加後の再計算対象を収集する
		void OnSceneInstancesChanged(ECSWorld& world, SystemContext& context, SceneChangePhase phase) override;

		//--------- accessor -----------------------------------------------------

		// システムの表示名を取得する
		const char* GetName() const override { return "TransformSystem"; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// トランスフォーム更新のためのスタックノード
		struct StackNode {

			Entity entity = Entity::Null(); // 更新対象
			bool updateWorld = false;		// 親の更新を子へ伝える
		};

		//--------- variables ----------------------------------------------------

		std::vector<Entity> dirtyTransforms_;	// 今回の再計算対象
		std::vector<Entity> queuedTransforms_;	// 次回の再計算対象
		std::vector<Entity> changedTransforms_; // 更新した変換の通知先
		std::vector<StackNode> stack_;			// 更新中の部分木
		uint64_t mutationListenerID_ = 0;		// 変更通知の購読ID
		uint32_t transformTypeID_ = 0;			// 変換Componentの型ID

		//--------- functions ----------------------------------------------------

		// 変換の変更通知を再計算キューへ積む
		static void OnComponentMutation(
			ECSWorld& world, const Entity& entity, uint32_t typeID, ComponentMutationKind kind, void* userData);
		// 再計算が必要な変換を接続時とScene変更時に収集する
		void QueueExistingDirtyTransforms(ECSWorld& world);
		// 再計算対象の先頭から子孫のワールド行列を更新する
		ComponentChangeChannel UpdateDirtySubtree(ECSWorld& world, const Entity& entity);
		// 再計算が必要な変換の階層を更新する
		void UpdateTransforms(ECSWorld& world);
	};
} // Engine
