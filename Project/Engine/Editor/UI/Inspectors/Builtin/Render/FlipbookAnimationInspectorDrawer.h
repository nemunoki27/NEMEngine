#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/FlipbookAnimationComponent.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	FlipbookAnimationInspectorDrawer class
	//	連番画像アニメーションコンポーネントのインスペクター描画
	//============================================================================
	class FlipbookAnimationInspectorDrawer :
		public SerializedComponentInspectorDrawer<FlipbookAnimationComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		FlipbookAnimationInspectorDrawer() :
			SerializedComponentInspectorDrawer("FlipbookAnimation", "FlipbookAnimation") {}
		~FlipbookAnimationInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// DynamicBufferから分離した行ごとの横タイル数
		std::vector<int32_t> tileColumnDraft_{ 1 };

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& anyItemActive) override;
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const FlipbookAnimationComponent& component) override;
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const FlipbookAnimationComponent& component,
			nlohmann::json& out) const override;
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const FlipbookAnimationComponent& previewComponent) override;
	};
} // Engine
