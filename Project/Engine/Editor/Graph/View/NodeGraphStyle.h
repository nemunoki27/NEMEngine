#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphTypes.h>

// c++
#include <array>
#include <string>
// imgui
#include <imgui.h>
// imgui-node-editor
#include <imgui_node_editor.h>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	NodeGraphStyle class
	//	NodeGraphの色設定を返すクラス
	//============================================================================
	class NodeGraphStyle {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// コンストラクタ
		NodeGraphStyle();

		// imgui-node-editorへStyleを反映する
		void PushEditorStyle() const;
		// Pushしたimgui-node-editorのStyleを戻す
		void PopEditorStyle() const;

		// Node種別に応じたアクセント色を取得する
		ImVec4 GetNodeAccentColor(const std::string& nodeType) const;
		// Pinの値型に応じた色を取得する
		ImVec4 GetPinColor(GraphValueType valueType) const;
		// Linkの値型に応じた色を取得する
		ImVec4 GetLinkColor(GraphValueType valueType) const;
		// Error表示色を取得する
		ImVec4 GetErrorColor() const;
		// Warning表示色を取得する
		ImVec4 GetWarningColor() const;

		// StyleをJSONへ変換する
		nlohmann::json ToJson() const;
		// JSONからStyleを読み込む
		void FromJson(const nlohmann::json& data);

		//--------- variables ----------------------------------------------------

		// Node内側の余白
		ImVec4 nodePadding{ 10.0f, 8.0f, 10.0f, 8.0f };
		// Nodeの横幅
		float nodeWidth = 260.0f;
		// Node内MyGUIのラベル横幅
		float nodeLabelWidth = 82.0f;
		// Nodeの角丸
		float nodeRounding = 7.0f;
		// 通常Nodeの枠線幅
		float nodeBorderWidth = 1.5f;
		// Hover中Nodeの枠線幅
		float hoveredNodeBorderWidth = 3.5f;
		// 選択中Nodeの枠線幅
		float selectedNodeBorderWidth = 3.5f;
		// Hover中Node枠線のOffset
		float hoveredNodeBorderOffset = 0.0f;
		// 選択中Node枠線のOffset
		float selectedNodeBorderOffset = 0.0f;
		// Pinの角丸
		float pinRounding = 4.0f;
		// Pinの枠線幅
		float pinBorderWidth = 0.0f;
		// Linkの曲がり強さ
		float linkStrength = 85.0f;
		// Link始点方向
		ImVec2 sourceDirection{ 1.0f, 0.0f };
		// Link終点方向
		ImVec2 targetDirection{ -1.0f, 0.0f };
		// Scroll移動時間
		float scrollDuration = 0.35f;
		// Flow Marker間隔
		float flowMarkerDistance = 30.0f;
		// Flow Marker速度
		float flowSpeed = 150.0f;
		// Flow Marker表示時間
		float flowDuration = 2.0f;
		// 入力Pin Pivotの基準位置
		ImVec2 inputPivotAlignment{ -0.5f, 0.5f };
		// 出力Pin Pivotの基準位置
		ImVec2 outputPivotAlignment{ 0.5f, 0.5f };
		// Pin Pivotのサイズ
		ImVec2 pivotSize{ 0.0f, 0.0f };
		// Pin PivotのScale
		ImVec2 pivotScale{ 1.0f, 1.0f };
		// Pinの角丸対象
		float pinCorners = 0.0f;
		// Pinの半径
		float pinRadius = 0.0f;
		// Pin Arrowのサイズ
		float pinArrowSize = 0.0f;
		// Pin Arrowの幅
		float pinArrowWidth = 0.0f;
		// Groupの角丸
		float groupRounding = 6.0f;
		// Groupの枠線幅
		float groupBorderWidth = 1.0f;
		// 接続Link強調の有効値
		float highlightConnectedLinks = 0.0f;
		// Pin方向へLinkを吸着するか
		float snapLinkToPinDir = 0.0f;

		// Clear Nodeのアクセント色
		ImVec4 clearNodeColor{ 0.35f, 0.50f, 0.72f, 1.0f };
		// DepthPrepass Nodeのアクセント色
		ImVec4 depthPrepassNodeColor{ 0.58f, 0.42f, 0.85f, 1.0f };
		// Draw / RenderScene Nodeのアクセント色
		ImVec4 drawNodeColor{ 0.34f, 0.72f, 0.42f, 1.0f };
		// Compute Nodeのアクセント色
		ImVec4 computeNodeColor{ 0.25f, 0.75f, 0.80f, 1.0f };
		// PostProcess Nodeのアクセント色
		ImVec4 postProcessNodeColor{ 0.78f, 0.34f, 0.76f, 1.0f };
		// Blit / FullscreenCopy Nodeのアクセント色
		ImVec4 blitNodeColor{ 0.85f, 0.55f, 0.25f, 1.0f };
		// Raytracing Nodeのアクセント色
		ImVec4 raytracingNodeColor{ 0.78f, 0.25f, 0.28f, 1.0f };
		// TemporaryTarget Nodeのアクセント色
		ImVec4 temporaryNodeColor{ 0.85f, 0.76f, 0.25f, 1.0f };
		// View Nodeのアクセント色
		ImVec4 viewNodeColor{ 0.95f, 0.80f, 0.35f, 1.0f };
		// 未分類Nodeのアクセント色
		ImVec4 defaultNodeColor{ 0.45f, 0.45f, 0.48f, 1.0f };

		// Flow Pinの色
		ImVec4 flowPinColor{ 1.00f, 0.74f, 0.30f, 1.0f };
		// Texture / RenderTarget Pinの色
		ImVec4 texturePinColor{ 0.35f, 0.68f, 0.95f, 1.0f };
		// Depth Pinの色
		ImVec4 depthPinColor{ 0.58f, 0.45f, 0.90f, 1.0f };
		// Asset Pinの色
		ImVec4 assetPinColor{ 0.75f, 0.55f, 0.95f, 1.0f };
		// View Pinの色
		ImVec4 viewPinColor{ 0.95f, 0.85f, 0.45f, 1.0f };
		// その他Pinの色
		ImVec4 defaultPinColor{ 0.70f, 0.70f, 0.72f, 1.0f };
		// Error表示色
		ImVec4 errorColor{ 0.95f, 0.28f, 0.25f, 1.0f };
		// Warning表示色
		ImVec4 warningColor{ 0.95f, 0.70f, 0.24f, 1.0f };

		// Link色のAlpha
		float linkAlpha = 0.90f;
		// 通常Linkの太さ
		float linkThickness = 2.0f;
		// Flow Linkの太さ
		float flowLinkThickness = 3.0f;
		// Link作成中の太さ
		float createLinkThickness = 2.0f;
		// 無効NodeのAlpha
		float disabledNodeAlpha = 0.45f;

		// imgui-node-editorの色
		std::array<ImVec4, ax::NodeEditor::StyleColor_Count> editorColors{};
	};
} // Engine
