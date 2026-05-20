#include "RenderPathGraphAssetDragDrop.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// imgui
#include <imgui.h>

//============================================================================
//	RenderPathGraphAssetDragDrop classMethods
//============================================================================

bool Engine::RenderPathGraphAssetDragDrop::AcceptMaterial(GraphNode& node, const EditorToolContext& context) {

	// Materialを持つPassだけDragDropを受け付ける
	if (node.type != RenderPathGraph::kPostProcess &&
		node.type != RenderPathGraph::kCompute &&
		node.type != RenderPathGraph::kBlit &&
		node.type != RenderPathGraph::kRaytracing) {
		return false;
	}
	if (!context.panelContext || !ImGui::BeginDragDropTarget()) {
		return false;
	}

	bool changed = false;
	if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {
		// ProjectPanelから渡されたAssetPayloadをMaterialとして受け取る
		if (payload->IsDelivery() && payload->DataSize == sizeof(EditorAssetDragDropPayload)) {
			const auto* assetPayload = static_cast<const EditorAssetDragDropPayload*>(payload->Data);
			if (assetPayload && assetPayload->assetType == AssetType::Material) {
				node.properties["material"] = ToString(assetPayload->assetID);
				// PostProcessはMaterial名が分かるようにNode名へ反映する
				node.displayName = node.type == RenderPathGraph::kPostProcess ?
					"PostProcess : " + std::string(assetPayload->assetPath) : node.displayName;
				changed = true;
			}
		}
	}
	ImGui::EndDragDropTarget();
	return changed;
}
