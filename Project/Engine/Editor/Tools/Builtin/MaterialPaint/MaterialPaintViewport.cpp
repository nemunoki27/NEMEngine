#include "MaterialPaintTool.h"
//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorPalette.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintMask.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <numbers>

namespace {

	using namespace Engine;

	ImVec2 ToScreen(const Vector3& position, const Matrix4x4& matrix, ImVec2 origin, ImVec2 size) {

		const Vector3 ndc = Vector3::Transform(position, matrix);
		return {origin.x + (0.5f + 0.5f * ndc.x) * size.x, origin.y + (0.5f - 0.5f * ndc.y) * size.y};
	}

	void DrawCircle(const Vector3& center, Vector3 normal, float radius, const Matrix4x4& matrix,
		ImVec2 origin, ImVec2 size, ImU32 color) {

		const Vector3 right = Vector3::Cross(std::abs(normal.y) < 0.9f ? Vector3(0,1,0) : Vector3(0,0,1), normal).Normalize();
		const Vector3 up = Vector3::Cross(normal, right);
		ImVec2 points[48];
		for (int i = 0; i < 48; ++i) {
			const float angle = (2 * std::numbers::pi_v<float> * i) / 48;
			points[i] = ToScreen(center + right * (radius * std::cos(angle)) + up * (radius * std::sin(angle)), matrix, origin, size);
		}
		ImGui::GetWindowDrawList()->AddPolyline(points, 48, color, ImDrawFlags_Closed, 1.5f);
	}
}

void Engine::MaterialPaintTool::UpdateHover(const EditorPanelContext& context, Vector2 pixel) {

	auto& graphics = *context.graphicsCore;
	if (!hoverPicker_) {
		hoverPicker_ = std::make_unique<MeshSubMeshPicker>();
		hoverPicker_->Init(graphics);
	}
	if (hoverPending_) {
		const auto result = hoverPicker_->ConsumePendingResult(graphics, context.GetWorld());
		if (result.resolved) {
			hoverEntity_ = result.hit ? result.entity : Entity::Null();
			hoverSubMesh_ = result.subMeshIndex;
			hoverPending_ = false;
		}
	}
	const bool samePosition = (hoverPixel_ - pixel).Length() < 5.0f;
	if (samePosition) {
		if (const char* message = HoverDiagnostic(context, hoverEntity_, hoverSubMesh_))
			ImGui::SetTooltip("%s", message);
	}
	if (hoverPending_ || ImGui::GetTime() < nextHoverPick_) return;
	auto* target = hoverPicker_->GetRenderTarget();
	if (target && context.renderPipeline->RenderMeshPicking(graphics, RenderViewKind::Scene, pixel, *target)) {
		hoverPending_ = hoverPicker_->ExecuteReadback(graphics, 1, *target);
		if (!samePosition) hoverEntity_ = Entity::Null();
		hoverPixel_ = pixel;
	}
	nextHoverPick_ = ImGui::GetTime() + 0.15;
}

void Engine::MaterialPaintTool::ApplyBrushSamples(const EditorPanelContext& context) {

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3);
	const auto image = surface_.GetImage();
	while (!samples_.empty() && std::chrono::steady_clock::now() < deadline) {
		const auto& sample = samples_.front();
		auto& target = targets_[targetIndex_];
		if (target.worldBatches.size() <= batchIndex_) {
			batchIndex_ = 0;
			if (++targetIndex_ == targets_.size()) { targetIndex_ = 0; samples_.pop_front(); }
			continue;
		}
		const auto& batch = target.worldBatches[batchIndex_];
		const auto& p = sample.point;
		const float r = brush_.radius;
		if (p.x + r < batch.min.x || batch.max.x < p.x - r || p.y + r < batch.min.y || batch.max.y < p.y - r ||
			p.z + r < batch.min.z || batch.max.z < p.z - r) { ++batchIndex_; continue; }
		const auto& tri = target.atlas->triangles[batch.first + triangleIndex_];
		const size_t sub = target.matrices.size() == 1 ? 0 : tri.subMesh;
		target.changed |= brush_.Stamp(tri, target.matrices[sub], image, p, sample.normal, sample.erase,
			target.channel, target.edit.after.resolution, target.mask, &target.dirty, &nextRow_);
		if (nextRow_ < 0 && ++triangleIndex_ == batch.count) { triangleIndex_ = 0; ++batchIndex_; }
	}
	UpdatePreviews(context);
}

bool Engine::MaterialPaintTool::DrawScene(const EditorPanelContext& context, ImVec2 origin, ImVec2 size, bool hovered) {

	if (!openWindow_ || !paintMode_ || !context.CanEditScene() || !context.host || !context.sceneRenderView ||
		!context.editorContext->assetDatabase || !context.renderPipeline) return false;
	const auto life = context.GetWorld()->GetLifetime();
	if (life != world_.lock()) { EndScenePreview(); world_ = life; }
	if (maskDatabase_ && !PublishMaterialPaintMasks(*context.editorContext->assetDatabase, false))
		diagnostic_ = "ペイントマスクを保存できません。シーン保存時に再試行します";
	if (waitingForMask_) ResolveCommittedMasks(context);
	const ImGuiID view = ImGui::GetID("##MaterialPaintStroke");
	if (stroke_ && strokeView_ != view) return false;
	if (stroke_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		EndScenePreview();
		return true;
	}
	if (stroke_ && (finishingStroke_ || ImGui::IsMouseReleased(ImGuiMouseButton_Left) || !ImGui::IsMouseDown(ImGuiMouseButton_Left))) {
		finishingStroke_ = true;
		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && 0 < size.x && 0 < size.y) {
			const auto mouse = ImGui::GetMousePos();
			pressPoint_ = {(mouse.x-origin.x)/size.x, (mouse.y-origin.y)/size.y};
			pressErase_ = ImGui::GetIO().KeyShift;
			pendingPress_ = true;
			preparingStroke_ = false;
		}
		ApplyBrushSamples(context);
		if (samples_.empty()) { FinishStroke(context); finishingStroke_ = false; }
		return true;
	}
	if (!hovered || context.editorState->sceneCameraNavigating || ImGui::GetIO().KeyAlt ||
		ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
		pendingPress_ = false;
		hasLastPoint_ = false;
		return stroke_;
	}
	if (!activeMaterial_ || !context.GetWorld()->IsAlive(context.editorState->selectedEntity)) return false;
	const auto* camera = context.sceneRenderView->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid || size.x <= 0 || size.y <= 0) return false;
	const auto& matrix = camera->matrices.viewProjectionMatrix;
	const auto mouse = ImGui::GetMousePos();
	const Vector2 pointer((mouse.x-origin.x)/size.x, (mouse.y-origin.y)/size.y);
	if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		pendingPress_ = true;
		preparingStroke_ = false;
		pressPoint_ = pointer;
		pressErase_ = ImGui::GetIO().KeyShift;
	}
	if (pendingPress_ && ImGui::IsKeyPressed(ImGuiKey_Escape)) pendingPress_ = false;
	if (!surface_.Update(context, matrix)) return true;
	const auto image = surface_.GetImage();
	const Vector2 uv = pendingPress_ ? pressPoint_ : pointer;
	if (!stroke_) UpdateHover(context, {uv.x * image.width, uv.y * image.height});
	Vector3 center;
	if (!image.Sample(uv.x, uv.y, center)) { pendingPress_ = hasLastPoint_ = false; return true; }
	const auto normalAt = [&](Vector2 coords, Vector3 position) {
		const Vector3 towardEye = camera->projectionMode == ResolvedProjectionMode::Orthographic ? -camera->forward :
			Vector3::NormalizeOr(camera->cameraPos - position, -camera->forward);
		return image.Normal(coords.x, coords.y, position, towardEye);
	};
	const Vector3 normal = normalAt(uv, center);
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
	const auto color = ImGui::GetColorU32(ImGuiCol_Text);
	DrawCircle(center, normal, brush_.radius, matrix, origin, size, color);
	if (0 < brush_.hardness && brush_.hardness < 1)
		DrawCircle(center, normal, brush_.radius * brush_.hardness, matrix, origin, size, ImGui::GetColorU32(ImGuiCol_TextDisabled));
	if (pendingPress_) {
		pendingPress_ = false;
		context.editorState->scenePickPressActive = false;
		context.editorState->scenePickClickPending = false;
		BeginStroke(context, pressErase_);
		if (stroke_) strokeView_ = view;
	}
	if (stroke_) {
		const Vector2 start = hasLastPoint_ ? lastPoint_ : uv;
		Vector3 previousCenter;
		const bool previousValid = hasLastPoint_ && image.Sample(start.x, start.y, previousCenter);
		const float distance = previousValid ? (center - previousCenter).Length() : 0;
		if (!hasLastPoint_ || distance >= brush_.radius * 0.12f) {
			// 画面上を連続して採取し、別の面へ渡った時も途中の空間を塗らない
			const float spanPixels = (Vector2((uv.x-start.x)*size.x, (uv.y-start.y)*size.y)).Length();
			const int steps = std::clamp(int(std::ceil(std::min(spanPixels + 1, distance / (brush_.radius * 0.12f)))), 1, 256);
			for (int i = 1; i <= steps; ++i) {
				const Vector2 q = start + (uv-start) * (float(i) / steps);
				Vector3 point;
				if (!image.Sample(q.x, q.y, point)) continue;
				samples_.push_back({point, normalAt(q, point), hasLastPoint_ ? ImGui::GetIO().KeyShift : pressErase_});
			}
			lastPoint_ = uv;
			hasLastPoint_ = true;
		}
		ApplyBrushSamples(context);
	}
	draw->PopClipRect();
	return true;
}
