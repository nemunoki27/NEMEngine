#include "LogicPlacementPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

#include <array>
#include <cmath>
#include <numbers>

using namespace Engine;

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
		if (preview.requestedFrame < ImGui::GetFrameCount() - 1 || preview.lifetime.lock() != world->GetLifetime() ||
			preview.scene != context.editorContext->activeSceneInstanceID || !preview.layout || !preview.layout->IsValid()) continue;
		if (preview.owner && (!world->IsAlive(context.editorState->selectedEntity) ||
			world->GetUUID(context.editorState->selectedEntity) != preview.owner)) continue;
		const auto* camera = context.sceneRenderView->FindCamera(preview.is2D ?
			RenderCameraDomain::Orthographic : RenderCameraDomain::Perspective);
		if (!camera || !camera->valid) continue;
		const auto& projection = camera->matrices.viewProjectionMatrix;
		Matrix4x4 inverse;
		if (!Matrix4x4::TryInverse(projection, inverse)) continue;
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
			if (preview.is2D) { draw->AddCircle(screen, 4, color, 16, 1.5f); continue; }
			auto ndc = Vector3::Transform(position, projection);
			ndc.x += 8 / imageSize.x;
			const float radius = (Vector3::Transform(ndc, inverse) - position).Length();
			if (!std::isfinite(radius)) continue;
			const std::array<Vector3, 3> axes{{{radius, 0, 0}, {0, radius, 0}, {0, 0, radius}}};
			for (size_t plane = 0; plane < axes.size(); ++plane) for (int32_t segment = 0; segment < 16; ++segment) {
				const float a = static_cast<float>(std::numbers::pi * 2 * segment / 16);
				const float b = static_cast<float>(std::numbers::pi * 2 * (segment + 1) / 16);
				ImVec2 from, to;
				if (project(position + axes[plane] * std::cos(a) + axes[(plane + 1) % 3] * std::sin(a), from) &&
					project(position + axes[plane] * std::cos(b) + axes[(plane + 1) % 3] * std::sin(b), to))
					draw->AddLine(from, to, color);
			}
		}
	}
	draw->PopClipRect();
}
