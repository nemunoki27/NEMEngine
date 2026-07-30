#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>

namespace Engine {

	//============================================================================
	//	UIComponentInspectorDrawers classes
	//============================================================================
	// キャンバス
	class CanvasInspectorDrawer :
		public SerializedComponentInspectorDrawer<CanvasComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		CanvasInspectorDrawer() : SerializedComponentInspectorDrawer("Canvas", "Canvas") {}
		~CanvasInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::vector<KeyDIKCode> navigationUpKeys_{};
		std::vector<KeyDIKCode> navigationDownKeys_{};
		std::vector<KeyDIKCode> navigationLeftKeys_{};
		std::vector<KeyDIKCode> navigationRightKeys_{};
		std::vector<GamePadButtons> navigationUpGamepadButtons_{};
		std::vector<GamePadButtons> navigationDownGamepadButtons_{};
		std::vector<GamePadButtons> navigationLeftGamepadButtons_{};
		std::vector<GamePadButtons> navigationRightGamepadButtons_{};
		std::vector<KeyDIKCode> submitKeys_{};
		std::vector<GamePadButtons> submitGamepadButtons_{};
		CanvasNavigationTable navigationTable_{};

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity,
			const CanvasComponent& component) override;
		void SerializeDraft(ECSWorld& world, const Entity& entity,
			const CanvasComponent& component, nlohmann::json& out) const override;
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const CanvasComponent& previewComponent) override;
	};

	class UISelectableInspectorDrawer :
		public SerializedComponentInspectorDrawer<UISelectableComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UISelectableInspectorDrawer() : SerializedComponentInspectorDrawer("UI Selectable", "UISelectable") {}
		~UISelectableInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const UISelectableComponent& previewComponent) override;
	};

	class UIImageButtonInspectorDrawer :
		public SerializedComponentInspectorDrawer<UIImageButtonComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIImageButtonInspectorDrawer() : SerializedComponentInspectorDrawer("UI Image Button", "UIImageButton") {}
		~UIImageButtonInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	class UITextButtonInspectorDrawer :
		public SerializedComponentInspectorDrawer<UITextButtonComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UITextButtonInspectorDrawer() : SerializedComponentInspectorDrawer("UI Text Button", "UITextButton") {}
		~UITextButtonInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};

	class UIProgressInspectorDrawer :
		public SerializedComponentInspectorDrawer<UIProgressComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		UIProgressInspectorDrawer() : SerializedComponentInspectorDrawer("UI Progress", "UIProgress") {}
		~UIProgressInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
		void ApplyPreview(ECSWorld& world, const Entity& entity,
			const UIProgressComponent& previewComponent) override;
	};

} // Engine
