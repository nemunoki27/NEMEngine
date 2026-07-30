#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Editor/UI/Inspectors/Common/ReflectedMaterialParameterDrawer.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	TextRendererInspectorDrawer class
	//============================================================================
	class TextRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<TextRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TextRendererInspectorDrawer() :
			SerializedComponentInspectorDrawer("Text Renderer", "TextRenderer") {
		}
		~TextRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// reflection駆動のマテリアルパラメータ描画
		ReflectedMaterialParameterDrawer materialParameterDrawer_{};
		// DynamicBufferから分離した文字別変換のドラフト
		std::vector<TextCharTransform> charTransformDraft_{};

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		// ワールドの文字別変換をドラフトへ同期する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const TextRendererComponent& component) override;
		// 文字別変換を含む保存データへ変換する
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const TextRendererComponent& component, nlohmann::json& out) const override;
		// 固定長設定と文字別変換をプレビューへ反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const TextRendererComponent& previewComponent) override;

		// フォント欄に.ttf/.otfがドロップされたらMSDFを生成し.font.jsonの参照へ差し替える
		void ResolveFontSourceDrop(const EditorPanelContext& context);
	};
} // Engine
