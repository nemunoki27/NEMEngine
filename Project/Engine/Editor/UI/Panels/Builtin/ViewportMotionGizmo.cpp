#include "ViewportMotionGizmo.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Animation/Motion/MotionAnimationEvaluation.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

void Engine::ViewportMotionGizmo::Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize) {

	auto* world = context.GetWorld();
	if (!world || !context.editorState || !context.sceneRenderView || imageSize.x <= 0 || imageSize.y <= 0) return;
	const auto entity = context.editorState->selectedEntity;
	if (!world->IsAlive(entity) || !IsEntityActiveInHierarchy(*world, entity)) return;
	const auto* motion = TryGetMotionAnimation(*world, entity);
	if (!motion || !motion->settings.enabled || !motion->settings.drawDebug) return;
	const auto* camera = context.sceneRenderView->FindCamera(
		context.editorState->sceneViewPickDimension == SceneViewPickDimension::Type2D ?
		RenderCameraDomain::Orthographic : RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) return;
	const auto points = GetMotionAnimationDebugPoints(*world, entity);
	const auto& matrix = camera->matrices.viewProjectionMatrix;
	const auto project = [&](Vector3 position, ImVec2& pixel) {
		const float w = position.x * matrix.m[0][3] + position.y * matrix.m[1][3] +
			position.z * matrix.m[2][3] + matrix.m[3][3];
		if (w <= 0.000001f) return false;
		const auto ndc = Vector3::Transform(position, matrix);
		if (ndc.z < 0 || ndc.z > 1) return false;
		pixel = ImVec2(imagePosition.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
			imagePosition.y + (0.5f - ndc.y * 0.5f) * imageSize.y);
		return true;
	};
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(imagePosition, ImVec2(imagePosition.x + imageSize.x, imagePosition.y + imageSize.y), true);
	for (size_t index = 1; index < points.size(); ++index) {
		ImVec2 start, end;
		if (project(points[index - 1], start) && project(points[index], end))
			draw->AddLine(start, end, IM_COL32(90, 230, 140, 255), 2);
	}
	if (!points.empty()) {
		ImVec2 start, end;
		if (project(points.front(), start)) draw->AddCircleFilled(start, 5, IM_COL32(90, 230, 140, 255));
		if (project(points.back(), end)) draw->AddCircle(end, 6, IM_COL32(255, 190, 65, 255), 0, 2);
	}
	draw->PopClipRect();
}
