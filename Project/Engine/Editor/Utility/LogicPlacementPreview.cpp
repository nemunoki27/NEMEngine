#include "LogicPlacementPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>

#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	bool IsVisible(const LogicPlacementPreview& preview, const EditorContext& context, const EditorState& state) {
		if (preview.requestedFrame < ImGui::GetFrameCount() - 1 || preview.lifetime.lock() != context.activeWorld->GetLifetime() ||
			preview.scene != context.activeSceneInstanceID || !preview.layout || !preview.layout->IsValid()) return false;
		if (preview.owner && (!context.activeWorld->IsAlive(state.selectedEntity) ||
			context.activeWorld->GetUUID(state.selectedEntity) != preview.owner)) return false;
		return true;
	}
}

void Engine::DrawLogicPlacementPreview3D(const EditorContext& context, const EditorState& state, const ResolvedRenderView& view) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (context.isPlaying || !context.activeWorld || !view.perspective.valid || !view.width) return;
	const bool requested = std::any_of(state.logicPlacementPreviews.begin(), state.logicPlacementPreviews.end(),
		[&](const auto& preview) { return !preview.is2D && IsVisible(preview, context, state); });
	if (!requested) return;
	const auto& projection = view.perspective.matrices.viewProjectionMatrix;
	Matrix4x4 inverse;
	if (!Matrix4x4::TryInverse(projection, inverse)) return;
	auto* renderer = LineRenderer::GetInstance()->Get3D();
	for (const auto& preview : state.logicPlacementPreviews) {
		if (preview.is2D || !IsVisible(preview, context, state)) continue;
		const Color4 color = Color4::FromHex(preview.owner ? 0xffbe41ff : 0x41d2f0ff);
		for (const auto& line : preview.layout->lines)
			renderer->DrawLine(Vector3::Transform(line.from, preview.transform), Vector3::Transform(line.to, preview.transform), color, 1.5f);
		for (const auto& point : preview.layout->points) {
			const auto position = Vector3::Transform(point.position, preview.transform);
			auto ndc = Vector3::Transform(position, projection);
			if (!(0 <= ndc.z && ndc.z <= 1)) continue;
			ndc.x += 8.0f / view.width;
			const float radius = (Vector3::Transform(ndc, inverse) - position).Length();
			if (std::isfinite(radius)) renderer->DrawSphere(position, radius, color);
		}
	}
#endif
}

void Engine::SubmitLogicPlacementPreview(const EditorPanelContext& context, std::shared_ptr<const LogicPlacementResult> layout,
	const Matrix4x4& transform, bool is2D, UUID splineOwner) {

	auto* world = context.GetWorld();
	if (!context.editorState || !world || !context.CanEditScene()) return;
	auto& preview = context.editorState->logicPlacementPreviews[splineOwner ? 1 : 0];
	preview.layout = std::move(layout);
	preview.transform = transform;
	preview.lifetime = world->GetLifetime();
	preview.scene = context.editorContext->activeSceneInstanceID;
	preview.owner = splineOwner;
	preview.is2D = is2D;
	preview.requestedFrame = ImGui::GetFrameCount();
}

void Engine::DrawLogicPlacementPreview(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize) {

	auto* world = context.GetWorld();
	if (!context.editorState || !world || !context.CanEditScene() || !context.sceneRenderView || imageSize.x <= 0 || imageSize.y <= 0) return;
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(imagePosition, ImVec2(imagePosition.x + imageSize.x, imagePosition.y + imageSize.y), true);
	for (const auto& preview : context.editorState->logicPlacementPreviews) {
		if (!preview.is2D || !IsVisible(preview, *context.editorContext, *context.editorState)) continue;
		const auto* camera = context.sceneRenderView->FindCamera(RenderCameraDomain::Orthographic);
		if (!camera || !camera->valid) continue;
		const auto& projection = camera->matrices.viewProjectionMatrix;
		const auto project = [&](Vector3 position, ImVec2& screen) {
			const float w = position.x * projection.m[0][3] + position.y * projection.m[1][3] +
				position.z * projection.m[2][3] + projection.m[3][3];
			if (!std::isfinite(w) || w <= 0.000001f) return false;
			const auto ndc = Vector3::Transform(position, projection);
			if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z) || ndc.z < 0 || ndc.z > 1) return false;
			screen = {imagePosition.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
				imagePosition.y + (0.5f - ndc.y * 0.5f) * imageSize.y};
			return true;
		};
		const ImU32 color = preview.owner ? IM_COL32(255, 190, 65, 255) : IM_COL32(65, 210, 240, 255);
		const auto clipLine = [&](Vector3& a, Vector3& b) {
			for (int32_t plane = 0; plane < 3; ++plane) {
				const auto distance = [&](Vector3 point) {
					const float w = point.x * projection.m[0][3] + point.y * projection.m[1][3] +
						point.z * projection.m[2][3] + projection.m[3][3];
					const float z = point.x * projection.m[0][2] + point.y * projection.m[1][2] +
						point.z * projection.m[2][2] + projection.m[3][2];
					return plane == 0 ? w - 0.00001f : plane == 1 ? z : w - z;
				};
				const float from = distance(a), to = distance(b);
				if (!std::isfinite(from) || !std::isfinite(to) || (from < 0 && to < 0)) return false;
				if (from < 0 || to < 0) {
					const auto intersection = Vector3::Lerp(a, b, from / (from - to));
					if (from < 0) a = intersection;
					else b = intersection;
				}
			}
			return true;
		};
		for (const auto& line : preview.layout->lines) {
			ImVec2 a, b;
			auto from = Vector3::Transform(line.from, preview.transform);
			auto to = Vector3::Transform(line.to, preview.transform);
			if (clipLine(from, to) && project(from, a) && project(to, b))
				draw->AddLine(a, b, color, 1.5f);
		}
		for (const auto& point : preview.layout->points) {
			const auto position = Vector3::Transform(point.position, preview.transform);
			ImVec2 screen;
			if (!project(position, screen) || screen.x < imagePosition.x - 8 || screen.y < imagePosition.y - 8 ||
				screen.x > imagePosition.x + imageSize.x + 8 || screen.y > imagePosition.y + imageSize.y + 8) continue;
			draw->AddCircle(screen, 4, color, 16, 1.5f);
		}
	}
	draw->PopClipRect();
}
