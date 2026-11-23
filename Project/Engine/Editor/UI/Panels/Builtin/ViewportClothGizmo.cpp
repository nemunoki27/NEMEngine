#include "ViewportClothGizmo.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

void Engine::ViewportClothGizmo::Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize) {

	auto* world = context.GetWorld();
	if (!world || !context.editorState || !context.sceneRenderView) return;
	const Entity owner = context.editorState->selectedEntity;
	if (!world->IsAlive(owner) || !IsEntityActiveInHierarchy(*world, owner)) return;
	const auto* cloth = TryGetCloth(static_cast<const ECSWorld&>(*world), owner);
	if (!cloth || !cloth->settings.enabled || !IsClothPlane(world->TryGetComponent<PrimitiveRendererComponent>(owner))) return;
	const auto& runtime = cloth->runtime;
	const auto* transform = world->TryGetComponent<TransformComponent>(owner);
	const auto* camera = context.sceneRenderView->FindCamera(RenderCameraDomain::Perspective);
	if (!transform || !camera || !camera->valid || runtime.pinned.size() != runtime.mesh.vertices.size()) return;
	const Matrix4x4 matrix = transform->worldMatrix * camera->matrices.viewProjectionMatrix;
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(imagePosition, ImVec2(imagePosition.x + imageSize.x, imagePosition.y + imageSize.y), true);
	for (size_t i = 0; i < runtime.pinned.size(); ++i) {
		if (!runtime.pinned[i]) continue;
		const auto& point = runtime.mesh.vertices[i].position;
		const float w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
		if (w <= 1.0e-6f) continue;
		const auto ndc = Vector3::Transform(point, matrix);
		if (ndc.z < 0 || 1 < ndc.z) continue;
		const ImVec2 pixel(imagePosition.x + (0.5f + ndc.x * 0.5f) * imageSize.x,
			imagePosition.y + (0.5f - ndc.y * 0.5f) * imageSize.y);
		draw->AddCircleFilled(pixel, 3.0f, IM_COL32(255, 196, 64, 255));
	}
	draw->PopClipRect();
}
