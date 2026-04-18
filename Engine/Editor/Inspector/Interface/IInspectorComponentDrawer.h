#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/EditorPanelContext.h>

namespace Engine {

	//============================================================================
	//	IInspectorComponentDrawer class
	//	Inspector内で各コンポーネントの描画責務を分離するための基底
	//============================================================================
	class IInspectorComponentDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IInspectorComponentDrawer() = default;
		virtual ~IInspectorComponentDrawer() = default;

		// 描画
		virtual void Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) = 0;

		//--------- accessor -----------------------------------------------------

		// 対象エンティティに対して描画できるか
		virtual bool CanDraw(ECSWorld& world, const Entity& entity) const = 0;
	};
} // Engine