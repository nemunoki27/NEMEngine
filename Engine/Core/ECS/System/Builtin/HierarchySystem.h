#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/System/Interface/ISystem.h>

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	HierarchySystem class
	//	階層構造の管理を行うシステム
	//============================================================================
	class HierarchySystem :
		public ISystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		HierarchySystem() = default;
		~HierarchySystem() = default;

		// ワールド切り替えで呼ばれる
		void OnWorldEnter(ECSWorld& world, SystemContext& context) override;

		// UUIDからランタイム実行用の親子関係を構築する
		void RebuildRuntimeLinks(ECSWorld& world, const std::vector<Entity>& scope);
		// 階層内のアクティブをルート以下で再計算する
		void RefreshActiveTree(ECSWorld& world, const Entity& root);

		//--------- accessor -----------------------------------------------------

		// エディタ操作、プレファブ、インスタンス化
		void SetParent(ECSWorld& world, const Entity& child, const Entity& newParent);

		const char* GetName() const override { return "HierarchySystem"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 子を親から切り離す
		void Detach(ECSWorld& world, const Entity& child);
		// 子を親の子リストの先頭に追加する
		void AttachLast(ECSWorld& world, const Entity& child, const Entity& parent);
		// 階層内のアクティブをルート以下で再計算するためのヘルパー
		void RefreshAllActiveStates(ECSWorld& world, const std::vector<Entity>& scope);
		void RefreshActiveRecursive(ECSWorld& world, const Entity& entity, bool parentActive);
	};
} // Engine