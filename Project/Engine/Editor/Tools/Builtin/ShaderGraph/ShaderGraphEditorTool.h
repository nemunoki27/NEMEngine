#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
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

		ShaderGraphEditorTool() = default;
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
		ShaderGraphAsset graph_{};
		bool graphLoaded_ = false;
		bool graphDirty_ = false;
		bool restoreNodePositions_ = false;
		int32_t selectedParameter_ = -1;
		std::string createAssetPath_ =
			"GameAssets/Materials/NewShader.shadergraph.json";
		std::string statusMessage_{};
		Vector2 createNodePosition_{};
		ax::NodeEditor::EditorContext* nodeEditor_ = nullptr;
		std::unordered_map<uintptr_t, PinAddress> pinAddresses_{};

		//--------- functions ----------------------------------------------------

		void DrawWindow(const EditorToolContext& context);
		void DrawToolbar(const EditorToolContext& context);
		void DrawParameterPanel(const EditorToolContext& context);
		void DrawGraph(const EditorToolContext& context);
		void DrawNode(ShaderGraphNode& node);
		void DrawNodeValue(ShaderGraphNode& node);
		void DrawNodeCreationMenu();
		void DrawParameterEditor(
			const EditorToolContext& context,
			ShaderGraphParameter& parameter);

		bool LoadGraph(const EditorToolContext& context, AssetID assetID);
		bool CreateGraph(const EditorToolContext& context);
		bool SaveAndCompile(const EditorToolContext& context);
		void CaptureNodePositions();
		void ResetNodeEditor();
		void RemoveNode(UUID nodeID);
		void RemoveParameter(uint32_t index);
		void AddNode(ShaderGraphNodeKind kind, Vector2 position);
		void AddConstantNode(
			ShaderGraphValueType type,
			Vector2 position);
		void AddParameterNode(UUID parameterID, Vector2 position);
	};
} // Engine
