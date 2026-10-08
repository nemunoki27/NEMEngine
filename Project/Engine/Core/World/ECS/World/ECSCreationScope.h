#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSChangeTracker.h>

// c++
#include <memory>

namespace Engine {

	class ECSWorldLifetime;

	//============================================================================
	//	ECSCreationScope class
	//	生成途中のEntityを成功確定まで保持する
	//============================================================================
	class ECSCreationScope {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ECSCreationScope(ECSWorld& world);
		~ECSCreationScope();
		ECSCreationScope(const ECSCreationScope&) = delete;
		ECSCreationScope& operator=(const ECSCreationScope&) = delete;

		// 生成結果をWorldへ引き渡す
		void Commit();
		// この範囲で生成したEntityだけを破棄する
		void Rollback();
		// この範囲で生成したEntityを即時破棄する
		void DestroyCreated(const Entity& entity);

		//--------- accessor -----------------------------------------------------

		bool HasCreations() const { return !created_.empty(); }
		bool Contains(const Entity& entity) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 生成と取消を行うWorld
		ECSWorld& world_;
		// 生成元のWorld終了を確認する
		std::shared_ptr<const ECSWorldLifetime> lifetime_;
		// 生成通知の購読番号
		uint64_t listenerID_ = 0;
		// 確定前に生成したEntity
		std::vector<Entity> created_;

		//--------- functions ----------------------------------------------------

		// Worldが生存している間だけ購読を解除する
		void EndRegistration();

		// 入れ子の生成も同じ範囲へ記録する
		static void OnMutation(
			ECSWorld& world, const Entity& entity, uint32_t typeID, ComponentMutationKind kind, void* userData);
	};
}
