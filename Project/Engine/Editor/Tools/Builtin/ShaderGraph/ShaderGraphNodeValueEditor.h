#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"

namespace Engine {

	//============================================================================
	//	ShaderGraphNodeValueEditor class
	//	Nodeの値編集とpopup状態を保持する
	//============================================================================
	class ShaderGraphNodeValueEditor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Nodeの値を編集する
		void DrawValue(ShaderGraphEditSession& session, ShaderGraphNode& node, float nodeWidth);
		// 予約した値編集popupを表示する
		void DrawPopup(ShaderGraphEditSession& session);
		// Graph切替時にpopupの参照を解除する
		void Reset();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		enum class NodeValuePopupKind : uint8_t {

			None,
			ValueType,
			Color,
		};

		//--------- variables ----------------------------------------------------

		UUID nodeValuePopupNode_{};
		NodeValuePopupKind nodeValuePopupKind_ = NodeValuePopupKind::None;
		Vector2 nodeValuePopupAnchor_{};
		float nodeValuePopupWidth_ = 0.0f;
		uint32_t nodeValuePopupViewportID_ = 0;
		bool requestNodeValuePopup_ = false;

		//--------- functions ----------------------------------------------------

		// 値編集popupの表示位置を予約する
		void RequestNodeValuePopup(
			UUID nodeID, NodeValuePopupKind kind, const Vector2& anchor, float width, uint32_t viewportID);
		// Nodeの型選択を表示する
		void DrawNodeValueTypeButton(ShaderGraphNode& node, float nodeWidth);
		// Nodeの色編集ボタンを表示する
		void DrawNodeColorButton(const ShaderGraphNode& node, const char* label, const Color4& value, float nodeWidth);
	};
} // Engine
