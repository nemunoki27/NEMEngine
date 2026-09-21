#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <cstdint>
#include <string>
// imgui
#include <imgui.h>
#include <imgui_internal.h>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	IEditorPanel structures
	//============================================================================
	// パネルの表示フェーズ
	enum class EditorPanelPhase {

		PreScene,
		PostScene,
	};

	// ドラッグ&ドロップのペイロード構造体
	struct EditorAssetDragDropPayload {

		AssetID assetID{};
		AssetType assetType = AssetType::Unknown;
		uint8_t isDirectory = 0;
		char assetPath[260]{};
	};

	//============================================================================
	//	IEditorPanel class
	//	エディタパネルのインターフェース
	//============================================================================
	class IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		IEditorPanel() = default;
		virtual ~IEditorPanel() = default;

		// 描画パネル
		virtual void Draw(const EditorPanelContext& context) = 0;
		// レイアウトへ保存するパネル固有状態を取得
		virtual nlohmann::json SaveLayoutState() const { return nlohmann::json::object(); }
		// レイアウトからパネル固有状態を復元
		virtual void LoadLayoutState([[maybe_unused]] const nlohmann::json& state) {}
		// 複製先へ渡すパネル固有状態を取得
		virtual nlohmann::json MakeDuplicateState([[maybe_unused]] const EditorPanelContext& context) const;

		//--------- accessor -----------------------------------------------------

		virtual EditorPanelPhase GetPhase() const { return EditorPanelPhase::PreScene; }
		virtual bool CanDuplicate([[maybe_unused]] const EditorPanelContext& context) const { return false; }
		const std::string& GetPanelTypeID() const { return panelTypeID_; }
		const std::string& GetInstanceID() const { return instanceID_; }
		bool IsPrimaryInstance() const { return primaryInstance_; }
		bool IsInstanceOpen() const { return instanceOpen_; }
		void SetInstanceOpen(bool open) { instanceOpen_ = open; }

		// パネルインスタンスを初期化
		void ConfigureInstance(const std::string& panelTypeID, const std::string& instanceID, bool primaryInstance);
		// ImGuiへ渡す固有ウィンドウ名を取得
		std::string MakeWindowName(const std::string& displayName) const;
		// タイトルバーの右クリックメニューを描画
		void DrawTitleBarContextMenu(const EditorPanelContext& context);
		// 実際に使用する表示フラグを取得
		bool* ResolveOpenState(bool* primaryOpenState);
		// 初回表示時のドック先を設定
		void SetInitialDockID(ImGuiID dockID) { initialDockID_ = dockID; }
		void ApplyInitialDock();
		ImGuiID GetCurrentDockID() const { return currentDockID_; }

		//--------- variables ----------------------------------------------------

		// ドラッグ&ドロップのペイロードタイプ
		// HIERARCHY
		static constexpr const char* kHierarchyDragDropPayloadType = "EDITOR_HIERARCHY_ENTITY_UUID";
		// ASSET
		static constexpr const char* kProjectAssetDragDropPayloadType = "EDITOR_PROJECT_ASSET";
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::string panelTypeID_;
		std::string instanceID_;
		bool primaryInstance_ = true;
		bool instanceOpen_ = true;
		ImGuiID currentDockID_ = 0;
		ImGuiID initialDockID_ = 0;
	};
} // Engine
