#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <unordered_set>
#include <deque>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	RuntimeWorldBaker class
	//	Authoring設定からRuntime向けの派生Componentを構築するクラス
	//============================================================================
	class RuntimeWorldBaker {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RuntimeWorldBaker() = default;
		~RuntimeWorldBaker();
		RuntimeWorldBaker(const RuntimeWorldBaker&) = delete;
		RuntimeWorldBaker& operator=(const RuntimeWorldBaker&) = delete;

		// Runtime Worldへ接続して変更通知を購読する
		void Attach(ECSWorld& world, AssetDatabase* assetDatabase);
		// 接続中のWorldから切り離す
		void Detach();

		// 接続中の全Entityを変換する
		void BakeAll();
		// 変更されたEntityだけを変換する
		void Flush();

		//--------- accessor -----------------------------------------------------

		// Runtime Worldへ接続されているか
		bool IsAttached() const { return worldLifetime_ && worldLifetime_->IsAlive(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 変換対象のRuntime World
		ECSWorld* world_ = nullptr;
		// Worldを保持せず終了状態を確認する
		std::shared_ptr<const ECSWorldLifetime> worldLifetime_;
		// Mesh等のアセット依存変換で参照するDatabase
		AssetDatabase* assetDatabase_ = nullptr;
		// Component変更通知の購読ID
		uint64_t mutationListenerID_ = 0;
		// 差分変換を待つEntity
		std::deque<Entity> dirtyEntities_;
		// 同じEntityを一度だけ積むためのキー
		std::unordered_set<uint64_t> dirtyEntityKeys_;
		// Bake中の内部変更を再登録しないためのフラグ
		bool baking_ = false;

		//--------- functions ----------------------------------------------------

		// Component変更通知を差分変換へ積む
		static void OnComponentMutation(ECSWorld& world, const Entity& entity,
			uint32_t typeID, ComponentMutationKind kind, void* userData);
		// 変換が必要なComponent種類か
		static bool IsBakeRelevant(uint32_t typeID);
		// Entityを差分変換へ一度だけ積む
		void MarkDirty(const Entity& entity);
		// 1Entityの派生Componentを構築する
		void BakeEntity(const Entity& entity);
		// Entityと世代から重複判定キーを作る
		static uint64_t MakeEntityKey(const Entity& entity);
	};
} // Engine
