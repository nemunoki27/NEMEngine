#include "ViewportDecalGizmo.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Rendering/Decals/DecalProjection.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

void Engine::ViewportDecalGizmo::Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize) {

	if (!context.sceneRenderView || !context.editorState) return;
	auto* world = context.GetWorld();
	const auto entity = context.editorState->selectedEntity;
	if (!world || !world->IsAlive(entity)) return;
	const auto* projector = world->TryGetComponent<DecalProjectorComponent>(entity);
	const auto* transform = world->TryGetComponent<TransformComponent>(entity);
	const auto* camera = context.sceneRenderView->FindCamera(RenderCameraDomain::Perspective);
	if (!projector || !transform || !camera || !camera->valid) return;
	DecalProjection projection;
	if (!DecalProjectionUtility::Build(*projector, transform->worldMatrix, projection)) return;
	const Matrix4x4 matrix = projection.decalToWorld * camera->matrices.viewProjectionMatrix;
	const auto project = [&](const Vector3& point, ImVec2& pixel) {
		const float w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
		if (w <= 1e-6f) return false;
		const Vector3 ndc = Vector3::Transform(point, matrix);
		pixel = ImVec2(imagePosition.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
			imagePosition.y + (0.5f - ndc.y * 0.5f) * imageSize.y);
		return true;
	};
	// 画像の外へ線をはみ出させない
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(imagePosition, ImVec2(imagePosition.x + imageSize.x, imagePosition.y + imageSize.y), true);
	ImVec2 points[8];
	bool valid[8];
	for (uint32_t index = 0; index < 8; ++index) {
		valid[index] = project(Vector3(index & 1u ? 0.5f : -0.5f, index & 2u ? 0.5f : -0.5f,
			index & 4u ? 0.5f : -0.5f), points[index]);
	}
	for (uint32_t index = 0; index < 8; ++index) {
		for (uint32_t axis = 0; axis < 3; ++axis) {
			const uint32_t next = index ^ (1u << axis);
			if (index < next && valid[index] && valid[next]) draw->AddLine(points[index], points[next], IM_COL32(255, 180, 65, 230), 1.5f);
		}
	}
	ImVec2 start, end, left, right;
	if (project(Vector3(0, 0, -0.5f), start) && project(Vector3(0, 0, 0.8f), end)) {
		draw->AddLine(start, end, IM_COL32(80, 180, 255, 255), 2.0f);
		if (project(Vector3(-0.1f, 0, 0.6f), left)) draw->AddLine(left, end, IM_COL32(80, 180, 255, 255), 2.0f);
		if (project(Vector3(0.1f, 0, 0.6f), right)) draw->AddLine(right, end, IM_COL32(80, 180, 255, 255), 2.0f);
	}
	draw->PopClipRect();
}
