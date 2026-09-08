#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphIR.h>
#include <Engine/Editor/Tools/Builtin/ShaderGraph/ShaderGraphHistory.h>

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
		struct PreviewState;
		// Node Editorへ適用するユーザー外観設定
		struct NodeAppearanceSetting {

			Color4 canvasBackground{};
			Color4 grid{};
			Color4 nodeBackground{};
			Color4 nodeBorder{};
			Color4 hoveredNodeBorder{};
			Color4 selectedNodeBorder{};
			Color4 nodeSelection{};
			Color4 nodeSelectionBorder{};
			Color4 link{};
			Color4 hoveredLinkBorder{};
			Color4 selectedLinkBorder{};
			Color4 highlightedLinkBorder{};
			Color4 linkSelection{};
			Color4 linkSelectionBorder{};
			Color4 pinSelection{};
			Color4 pinSelectionBorder{};

			Vector4 nodePadding{};
			Vector2 linkStartOffset{};
			Vector2 linkEndOffset{};
			float nodeMinimumWidth = 180.0f;
			float nodePreviewDisplaySize = 144.0f;
			float nodeRounding = 0.0f;
			float nodeBorderWidth = 0.0f;
			float hoveredNodeBorderWidth = 0.0f;
			float selectedNodeBorderWidth = 0.0f;
			float nodeTextScale = 1.0f;
			float gridSpacing = 32.0f;
			float nodeSnapGridSize = 16.0f;
			float pinRounding = 0.0f;
			float pinBorderWidth = 0.0f;
			float linkStrength = 0.0f;
			float linkThickness = 0.0f;
			int32_t nodePreviewTextureSize = 128;
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
		AssetID selectedAsset_{};
		AssetID pendingAsset_{};
		AssetID previewMaterial_{};
		ShaderGraphAsset graph_{};
		ShaderGraphHistory history_{};
		bool graphLoaded_ = false;
		bool graphDirty_ = false;
		bool previewCompileDirty_ = false;
		bool commandPanelFocused_ = false;
		bool restoreNodePositions_ = false;
		int32_t selectedParameter_ = -1;
		int32_t selectedKeyword_ = -1;
		std::string createAssetPath_ =
			"GameAssets/Materials/NewShader.shadergraph.json";
		ShaderGraphDomain createDomain_ = ShaderGraphDomain::Surface;
		ShaderGraphTarget createTarget_ = ShaderGraphTarget::Mesh;
		std::string statusMessage_{};
		std::string nodeSearch_{};
		Vector2 createNodePosition_{};
		UUID contextNode_{};
		UUID editingGroup_{};
		bool requestGroupNameFocus_ = false;
		ax::NodeEditor::EditorContext* nodeEditor_ = nullptr;
		std::unordered_map<uintptr_t, PinAddress> pinAddresses_{};
		NodeAppearanceSetting appearanceSetting_{};
		UUID nodeValuePopupNode_{};
		NodeValuePopupKind nodeValuePopupKind_ =
			NodeValuePopupKind::None;
		Vector2 nodeValuePopupAnchor_{};
		float nodeValuePopupWidth_ = 0.0f;
		uint32_t nodeValuePopupViewportID_ = 0;
		bool requestNodeValuePopup_ = false;
		std::unique_ptr<PreviewState> previewState_{};
		UUID previewEntityUUID_{};
		UUID appliedPreviewEntityUUID_{};
		ShaderGraphTarget appliedPreviewTarget_ =
			ShaderGraphTarget::Mesh;
		AssetID previewOriginalMaterial_{};
		bool previewMaterialApplied_ = false;
		double previewCompileDeadline_ = 0.0;
		std::string compiledGraphState_{};
		std::vector<ShaderGraphDiagnostic> latestDiagnostics_{};

		//--------- functions ----------------------------------------------------

		void DrawWindow(const EditorToolContext& context);
		void DrawToolbar(const EditorToolContext& context);
		void DrawParameterPanel(const EditorToolContext& context);
		void DrawAppearancePanel();
		void DrawGraph(const EditorToolContext& context);
		void DrawGroup(ShaderGraphGroup& group);
		void DrawNode(ShaderGraphNode& node);
		void DrawNodePreview(
			ShaderGraphNode& node,
			float nodeWidth);
		void DrawNodePins(
			const ShaderGraphNode& node,
			float nodeWidth);
		void DrawNodeSeparator(float nodeWidth) const;
		float CalculateNodeWidth(
			const ShaderGraphNode& node) const;
		void DrawNodeValue(
			ShaderGraphNode& node,
			float nodeWidth);
		void DrawNodeValuePopup();
		void RequestNodeValuePopup(
			UUID nodeID,
			NodeValuePopupKind kind,
			const Vector2& anchor,
			float width,
			uint32_t viewportID);
		void DrawNodeValueTypeButton(
			ShaderGraphNode& node,
			float nodeWidth);
		void DrawNodeColorButton(
			const ShaderGraphNode& node,
			const char* label,
			const Color4& value,
			float nodeWidth);
		void DrawContextMenus();
		void DrawNodeCreationMenu();
		void DrawGraphSettings(const EditorToolContext& context);
		void DrawKeywordEditor();
		void DrawSelectedNodeEditor(const EditorToolContext& context);
		void DrawDiagnostics();
		void DrawParameterEditor(
			const EditorToolContext& context,
			ShaderGraphParameter& parameter);
		void DrawPreviewSetting(
			const EditorToolContext& context);

		bool LoadGraph(const EditorToolContext& context, AssetID assetID);
		// 別アセットの設定を検証して一括置換する
		void ImportGraphSettings(const EditorToolContext& context, AssetID source);
		bool CreateGraph(const EditorToolContext& context);
		bool SaveAndCompile(const EditorToolContext& context);
		bool ApplyPreviewMaterial(
			const EditorToolContext& context);
		void RestorePreviewMaterial(
			const EditorToolContext& context);
		void UpdateMaterialPreview(
			const EditorToolContext& context);
		bool LoadAppearanceSettings();
		void SaveAppearanceSettings() const;
		void ApplyAppearanceSettings();
		void RestoreDefaultAppearance();
		void ClampAppearanceSettings();
		void CaptureNodePositions();
		void ResetNodeEditor();
		void UpdateNodePreviews(
			const EditorToolContext& context);
		void InvalidateNodePreviews();
		void ClearNodePreviews();
		void RemoveNode(UUID nodeID);
		void RemoveGroup(UUID groupID);
		void DuplicateNode(UUID nodeID);
		void DuplicateGroup(UUID groupID);
		void CopySelection();
		void PasteSelection();
		void UndoGraph();
		void RedoGraph();
		void CommitGraphHistory();
		void RemoveParameter(uint32_t index);
		std::vector<UUID> GetSelectedGraphNodes() const;
		void GroupSelectedNodes();
		void AddNode(ShaderGraphNodeKind kind, Vector2 position);
		void AddConstantNode(
			ShaderGraphValueType type,
			Vector2 position);
		void AddParameterNode(UUID parameterID, Vector2 position);
		void AddKeywordNode(UUID keywordID, Vector2 position);
	};
} // Engine
