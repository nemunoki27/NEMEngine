#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionFrameBuilder.h"

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	CollisionContactHistory class
	//	World内の接触履歴と対になる通知を管理する
	//============================================================================
	class CollisionContactHistory {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		using Contacts = std::unordered_map<CollisionPairKey, CollisionContact, CollisionPairKeyHash>;

		// 接触履歴を破棄
		void Clear() { previousContacts_.clear(); }
		// 押し戻し後の接触開始または継続を通知
		void NotifyContact(ECSWorld& world, SystemContext& context, const CollisionPairKey& key, const CollisionContact& contact) const;
		// 接触終了を通知して今回の履歴へ更新
		void Commit(ECSWorld& world, SystemContext& context, Contacts currentContacts);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Contacts previousContacts_;

		//--------- functions ----------------------------------------------------

		// 接触開始を両Entityへ通知
		void DispatchCollisionEnter(ECSWorld& world, SystemContext& context, const CollisionContact& contact) const;
		// 接触継続を両Entityへ通知
		void DispatchCollisionStay(ECSWorld& world, SystemContext& context, const CollisionContact& contact) const;
		// 接触終了を両Entityへ通知
		void DispatchCollisionExit(ECSWorld& world, SystemContext& context, const CollisionContact& contact) const;
	};
}
