#include "ShaderGraphPreviewController.h"

#include "ShaderGraphEditSession.h"
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAssetReferences.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSubGraphExpander.h>

#include <algorithm>

namespace {

	bool UsesTime(const Engine::ShaderGraphAsset& graph, const Engine::AssetDatabase* database) {

		using namespace Engine;
		const auto timeInput = [](const ShaderGraphNode& node) {
			return node.kind == ShaderGraphNodeKind::Time || node.kind == ShaderGraphNodeKind::CustomFunction;
		};
		if (std::any_of(graph.nodes.begin(), graph.nodes.end(), timeInput)) return true;
		if (std::none_of(graph.nodes.begin(), graph.nodes.end(), [](const ShaderGraphNode& node) {
			return node.kind == ShaderGraphNodeKind::SubGraph;
		})) return false;

		ShaderGraphAsset expanded = graph;
		std::vector<ShaderGraphDiagnostic> diagnostics;
		const auto resolve = [database](AssetID asset, ShaderGraphAsset& child) {
			return ShaderGraphAssetReferences::Load(asset, database, child);
		};
		return ShaderGraphSubGraphExpander::Expand(expanded, resolve, diagnostics) &&
			std::any_of(expanded.nodes.begin(), expanded.nodes.end(), timeInput);
	}
}

Engine::ShaderGraphPreviewController::ShaderGraphPreviewController(ShaderGraphEditSession& session)
	: session_(session) {
}

void Engine::ShaderGraphPreviewController::Draw(const EditorToolContext& context, ImVec2 origin, ImVec2 canvasSize) {

	constexpr float margin = 8.0f;
	const float width = (std::min)(canvasSize.x * 0.2f, canvasSize.y * 0.4f);
	const ImVec2 size(width, width * 0.75f);
	if (width < 1.0f || canvasSize.x < width + margin || canvasSize.y < size.y + margin || !context.panelContext) return;
	const ImVec2 pos(origin.x + canvasSize.x - size.x - margin, origin.y + canvasSize.y - size.y - margin);
	ImGui::SetCursorScreenPos(pos);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
	if (ImGui::BeginChild("ShaderGraphOutputPreview", size, ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
		const auto& panel = *context.panelContext;
		const bool switched = graph_ != session_.GetAssetID();
		if (switched || revision_ != session_.GetPreviewRevision()) {
			graph_ = session_.GetAssetID();
			revision_ = session_.GetPreviewRevision();
			deadline_ = ImGui::GetTime() + (switched ? 0.0 : 0.25);
			if (switched) failed_ = false;
		}
		if (panel.renderPipeline && session_.NeedsCompile() && ImGui::GetTime() >= deadline_) {
			failed_ = !session_.CompilePreview(context);
			if (!failed_) {
				const auto& graph = session_.GetDraft();
				animated_ = graph.domain == ShaderGraphDomain::Surface &&
					(graph.target == ShaderGraphTarget::Particle || graph.target == ShaderGraphTarget::Trail);
				animated_ |= UsesTime(graph, context.toolContext.assetDatabase);
				preview_.Invalidate();
			}
		}
		if (panel.renderPipeline && session_.GetPreviewMaterialID()) {
			if (const auto* material = panel.renderPipeline->GetRenderAssetLibrary().LoadMaterial(session_.GetPreviewMaterialID())) {
				preview_.DrawImage(panel, *material, size, animated_ && !failed_);
			}
		}
		if (failed_) {
			ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), "更新失敗");
		}
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}
