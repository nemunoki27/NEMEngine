#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Common/ReflectedMaterialParameterDrawer.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>

namespace Engine {

	//============================================================================
	//	SpriteRendererInspectorDrawer class
	//	スプライトレンダラーコンポーネントのインスペクター描画
	//============================================================================
	class SpriteRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<SpriteRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SpriteRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Sprite Renderer", "SpriteRenderer") {
		}
		~SpriteRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// reflection駆動のマテリアルパラメータ描画
		ReflectedMaterialParameterDrawer materialParameterDrawer_{};

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engin
