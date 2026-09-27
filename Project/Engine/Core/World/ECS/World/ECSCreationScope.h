#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSChangeTracker.h>

namespace Engine {

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

		ECSWorld& world_;
		uint64_t listenerID_ = 0;
		std::vector<Entity> created_;

		//--------- functions ----------------------------------------------------

		// 入れ子の生成も同じ範囲へ記録する
		static void OnMutation(ECSWorld& world, const Entity& entity,
			uint32_t typeID, ComponentMutationKind kind, void* userData);
	};
}
