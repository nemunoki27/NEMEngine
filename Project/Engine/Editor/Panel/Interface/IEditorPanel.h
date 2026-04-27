#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/EditorPanelContext.h>
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <cstdint>
// imgui
#include <imgui.h>
#include <imgui_internal.h>

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
		//========================================================================
		//	public Methods
		//========================================================================

		IEditorPanel() = default;
		virtual ~IEditorPanel() = default;

		// 描画パネル
		virtual void Draw(const EditorPanelContext& context) = 0;

		//--------- accessor -----------------------------------------------------

		virtual EditorPanelPhase GetPhase() const { return EditorPanelPhase::PreScene; }

		//--------- variables ----------------------------------------------------

		// ドラッグ&ドロップのペイロードタイプ
		// HIERARCHY
		static constexpr const char* kHierarchyDragDropPayloadType = "EDITOR_HIERARCHY_ENTITY_UUID";
		// ASSET
		static constexpr const char* kProjectAssetDragDropPayloadType = "EDITOR_PROJECT_ASSET";
	};
} // Engine
