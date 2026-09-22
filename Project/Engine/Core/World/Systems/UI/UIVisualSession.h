#pragma once

//============================================================================
//	include
//============================================================================
#include "UIInputTypes.h"

// c++
#include <unordered_map>
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	UIVisualSession class
	//	UI状態アニメーションと復元値を所有する
	//============================================================================
	class UIVisualSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 対象外になったUIの表示を復元
		void DiscardInactive(ECSWorld& world, const std::unordered_set<UUID>& activeSelectableEntities);
		// 再度決定したUIの状態遷移を戻す
		void ResetSubmitState(UUID entityUUID);
		// 状態アニメーションを更新
		UISelectableAnimationRuntime* UpdateAnimation(ECSWorld& world, UISelectableEntry& entry, SystemContext& context);
		// 全ての適用値を復元して破棄
		void RestoreAll(ECSWorld& world);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<UUID, UISelectableAnimationRuntime> animationRuntimes_;
	};
}
