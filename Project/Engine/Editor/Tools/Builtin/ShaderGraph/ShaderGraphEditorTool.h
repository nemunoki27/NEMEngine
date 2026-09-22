#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "ShaderGraphAppearance.h"
#include "ShaderGraphScenePreview.h"
#include "ShaderGraphNodePreviews.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphIR.h>
#include "ShaderGraphEditSession.h"

// c++
#include <memory>
#include <string>
#include <unordered_map>

namespace ax::NodeEditor {

	struct EditorContext;
}

namespace Engine {

	//============================================================================
	//	ShaderGraphEditorTool class
	//	Surfaceグラフの編集とHLSL/Material生成を行う
	//============================================================================
	class ShaderGraphEditorTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ShaderGraphEditorTool();
		~ShaderGraphEditorTool() override;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		// ProjectPanelから指定グラフを開く
		void OpenAsset(AssetID assetID);

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		struct PinAddress {

			UUID node{};
			uint32_t slot = 0;
			bool input = false;
		};
		enum class NodeValuePopupKind : uint8_t {

			None,
			ValueType,
			Color,
		};

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.shader_graph",
			.name = "シェーダーグラフ",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 2,
		};

		bool openWindow_ = false;
		AssetID pendingAsset_{};
		ShaderGraphEditSession editSession_;
		bool commandPanelFocused_ = false;
		bool restoreNodePositions_ = false;
		int32_t selectedParameter_ = -1;
		int32_t selectedKeyword_ = -1;
		std::string createAssetPath_ =
			"GameAssets/Materials/NewShader.shadergraph.json";
		ShaderGraphDomain createDomain_ = ShaderGraphDomain::Surface;
		ShaderGraphTarget createTarget_ = ShaderGraphTarget::Mesh;
		std::string nodeSearch_{};
		Vector2 createNodePosition_{};
		UUID contextNode_{};
		UUID editingGroup_{};
		bool requestGroupNameFocus_ = false;
		ax::NodeEditor::EditorContext* nodeEditor_ = nullptr;
		std::unordered_map<uintptr_t, PinAddress> pinAddresses_{};
		ShaderGraphAppearanceSetting appearanceSetting_{};
		UUID nodeValuePopupNode_{};
		NodeValuePopupKind nodeValuePopupKind_ =
			NodeValuePopupKind::None;
		Vector2 nodeValuePopupAnchor_{};
		float nodeValuePopupWidth_ = 0.0f;
		uint32_t nodeValuePopupViewportID_ = 0;
		bool requestNodeValuePopup_ = false;
		ShaderGraphNodePreviews nodePreviews_;
		// シーン上のMaterialプレビュー
		ShaderGraphScenePreview scenePreview_;

		//--------- functions ----------------------------------------------------

		// ツールの編集画面を表示する
		void DrawWindow(const EditorToolContext& context);
		// 保存と履歴操作を表示する
		void DrawToolbar(const EditorToolContext& context);
		// 公開Parameterの一覧を表示する
		void DrawParameterPanel(const EditorToolContext& context);
		// Node Editorの外観を編集する
		void DrawAppearancePanel();
		// GraphのNodeと接続を表示する
		void DrawGraph(const EditorToolContext& context);
		// Groupの範囲と名前を表示する
		void DrawGroup(ShaderGraphGroup& group);
		// Nodeの本体を表示する
		void DrawNode(ShaderGraphNode& node);
		// Nodeの入出力Pinを表示する
		void DrawNodePins(const ShaderGraphNode& node, float nodeWidth);
		// Node内の区切り線を描く
		void DrawNodeSeparator(float nodeWidth) const;
		// Nodeの内容から表示幅を求める
		float CalculateNodeWidth(
			const ShaderGraphNode& node) const;
		// Nodeの値を編集する
		void DrawNodeValue(ShaderGraphNode& node, float nodeWidth);
		// Nodeの値編集popupを表示する
		void DrawNodeValuePopup();
		// Nodeの値編集popupを予約する
		void RequestNodeValuePopup(
			UUID nodeID,
			NodeValuePopupKind kind,
			const Vector2& anchor,
			float width,
			uint32_t viewportID);
		// Nodeの値の型を選択する
		void DrawNodeValueTypeButton(ShaderGraphNode& node, float nodeWidth);
		// Nodeの色編集ボタンを表示する
		void DrawNodeColorButton(const ShaderGraphNode& node, const char* label, const Color4& value, float nodeWidth);
		// 右クリック操作を表示する
		void DrawContextMenus();
		// 追加するNodeを選択する
		void DrawNodeCreationMenu();
		// Graphの描画設定を編集する
		void DrawGraphSettings(const EditorToolContext& context);
		// Keywordを編集する
		void DrawKeywordEditor();
		// 選択Nodeの詳細を編集する
		void DrawSelectedNodeEditor(const EditorToolContext& context);
		// コンパイル診断を表示する
		void DrawDiagnostics();
		// 公開Parameterの設定を編集する
		void DrawParameterEditor(const EditorToolContext& context, ShaderGraphParameter& parameter);
		// Sceneへのプレビュー設定を編集する
		void DrawPreviewSetting(const EditorToolContext& context);

		// Graphを読み編集画面を切り替える
		bool LoadGraph(const EditorToolContext& context, AssetID assetID);
		// 別アセットの設定を検証して一括置換する
		void ImportGraphSettings(const EditorToolContext& context, AssetID source);
		// 初期Graphを作成して開く
		bool CreateGraph(const EditorToolContext& context);
		// 位置を確定してGraphの保存とコンパイルを要求する
		bool SaveAndCompile(const EditorToolContext& context);
		// SceneへプレビューMaterialを適用する
		bool ApplyPreviewMaterial(const EditorToolContext& context);
		// Sceneの元のMaterialを復元する
		void RestorePreviewMaterial(const EditorToolContext& context);
		// 編集内容をプレビューへ反映する
		void UpdateMaterialPreview(const EditorToolContext& context);
		// 外観設定を読み込む
		bool LoadAppearanceSettings();
		// 外観設定を保存する
		void SaveAppearanceSettings() const;
		// 外観設定をNode Editorへ適用する
		void ApplyAppearanceSettings();
		// 既定の外観へ戻す
		void RestoreDefaultAppearance();
		// 外観設定を有効範囲へ収める
		void ClampAppearanceSettings();
		// Node Editorの位置をGraphへ取り込む
		void CaptureNodePositions();
		// Node Editorの表示状態を再作成する
		void ResetNodeEditor();
		// Nodeと関連する接続を削除する
		void RemoveNode(UUID nodeID);
		// Groupの所属を解除して削除する
		void RemoveGroup(UUID groupID);
		// Nodeを複製して配置する
		void DuplicateNode(UUID nodeID);
		// Group内のNodeを複製して配置する
		void DuplicateGroup(UUID groupID);
		// 選択Nodeをクリップボードへコピーする
		void CopySelection();
		// クリップボードのNodeを配置する
		void PasteSelection();
		// Graphの編集を元に戻す
		void UndoGraph();
		// Graphの編集をやり直す
		void RedoGraph();
		// 編集内容をGraphの履歴へ確定する
		void CommitGraphHistory();
		// Parameterと参照Nodeを削除する
		void RemoveParameter(uint32_t index);
		// 選択しているNodeを取得する
		std::vector<UUID> GetSelectedGraphNodes() const;
		// 選択NodeをGroupへまとめる
		void GroupSelectedNodes();
		// 指定種類のNodeを追加する
		void AddNode(ShaderGraphNodeKind kind, Vector2 position);
		// 定数Nodeを追加する
		void AddConstantNode(ShaderGraphValueType type, Vector2 position);
		// Parameter参照Nodeを追加する
		void AddParameterNode(UUID parameterID, Vector2 position);
		// Keyword参照Nodeを追加する
		void AddKeywordNode(UUID keywordID, Vector2 position);
	};
} // Engine
