#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	HierarchySystem class
	//	階層構造の管理を行うシステム
	//============================================================================
	class HierarchySystem : public ISystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		HierarchySystem() = default;
		~HierarchySystem() = default;

		// ワールド切り替えで呼ばれる
		void OnWorldEnter(ECSWorld& world, SystemContext& context) override;

		// UUIDからランタイム実行用の親子関係を構築する
		void RebuildRuntimeLinks(ECSWorld& world, const std::vector<Entity>& scope);
		// 階層内のアクティブをルート以下で再計算する
		void RefreshActiveTree(ECSWorld& world, const Entity& root);
		// 指定エンティティ以下のアクティブ状態を親を考慮して更新する
		void UpdateActiveInHierarchy(ECSWorld& world, const Entity& entity);
		// 階層外の親も含めてアクティブ状態を子孫へ伝播する
		bool RefreshActiveRecursive(ECSWorld& world, const Entity& entity, bool parentActive);
		// 親子関係と子孫のアクティブ状態を更新する
		void SetParent(ECSWorld& world, const Entity& child, const Entity& newParent);

		//--------- accessor -----------------------------------------------------

		// システムの表示名を取得する
		const char* GetName() const override { return "HierarchySystem"; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 子を親から切り離す
		void Detach(ECSWorld& world, const Entity& child);
		// 子を親の子リストの末尾に追加する
		void AttachLast(ECSWorld& world, const Entity& child, const Entity& parent);
		// 階層内のアクティブをルート以下で再計算するためのヘルパー
		void RefreshAllActiveStates(ECSWorld& world, const std::vector<Entity>& scope);
	};
} // Engine
