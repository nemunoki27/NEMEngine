#include "ViewportSplineGizmo.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

using namespace Engine;

ViewportSplineGizmo::~ViewportSplineGizmo() {

	EndPreview();
}

void ViewportSplineGizmo::EndPreview() {

	const auto lifetime = lifetime_.lock();
	if (point_ >= 0 && lifetime && lifetime->IsAlive() && world_) {
		const auto entity = world_->FindByUUID(owner_);
		if (world_->IsAlive(entity) && world_->HasComponent<SplineComponent>(entity)) world_->ApplyComponentJson(entity, "Spline", before_);
	}
	point_ = -1;
	world_ = nullptr;
	lifetime_.reset();
	before_ = {};
}

bool ViewportSplineGizmo::Draw(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize) {

	auto* world = context.GetWorld();
	if (!world || !context.editorState || !context.sceneRenderView || imageSize.x <= 0 || imageSize.y <= 0) {
		EndPreview();
		return false;
	}
	const auto entity = context.editorState->selectedEntity;
	if (point_ >= 0 && (world != world_ || lifetime_.lock() != world->GetLifetime() || owner_ != world->GetUUID(entity))) EndPreview();
	const auto* data = TryGetSpline(*world, entity);
	const auto* transform = world->TryGetComponent<TransformComponent>(entity);
	const auto* camera = context.sceneRenderView->FindCamera(RenderCameraDomain::Perspective);
	if (!data || !transform || !camera || !camera->valid || !context.CanEditScene()) { EndPreview(); return false; }
	if (context.editorState->sceneCameraNavigating || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
		ImGui::IsKeyPressed(ImGuiKey_Escape)) { EndPreview(); return false; }
	Matrix4x4 inverseWorld;
	Matrix4x4 inverseView;
	const auto& viewProjection = camera->matrices.viewProjectionMatrix;
	if (!Matrix4x4::TryInverse(transform->worldMatrix, inverseWorld) || !Matrix4x4::TryInverse(viewProjection, inverseView)) return false;
	const auto project = [&](Vector3 local, ImVec2& screen, float& depth) {
		const auto worldPosition = Vector3::Transform(local, transform->worldMatrix);
		const float w = worldPosition.x * viewProjection.m[0][3] + worldPosition.y * viewProjection.m[1][3] +
			worldPosition.z * viewProjection.m[2][3] + viewProjection.m[3][3];
		if (w <= 1e-6f) return false;
		const auto ndc = Vector3::Transform(worldPosition, viewProjection);
		screen = ImVec2(imagePosition.x + (ndc.x * 0.5f + 0.5f) * imageSize.x,
			imagePosition.y + (0.5f - ndc.y * 0.5f) * imageSize.y);
		depth = ndc.z;
		return true;
	};
	auto* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(imagePosition, ImVec2(imagePosition.x + imageSize.x, imagePosition.y + imageSize.y), true);
	const auto samples = BuildSplineSamples(*data, Matrix4x4::Identity());
	for (size_t i = 1; i < samples.size(); ++i) {
		ImVec2 a, b;
		float depth;
		if (project(samples[i - 1].position, a, depth) && project(samples[i].position, b, depth))
			draw->AddLine(a, b, IM_COL32(255, 190, 65, 255), 2);
	}
	const auto mouse = ImGui::GetMousePos();
	int32_t hoverPoint = -1;
	uint32_t hoverPart = 0;
	float hoverDepth = 0;
	float bestDistance = 64;
	for (size_t i = 0; i < data->points.size(); ++i) {
		const auto& point = data->points[i];
		ImVec2 pixels[3];
		bool visible[3];
		float depths[3];
		const Vector3 handles[3] = {point.position, point.position + point.tangentIn, point.position + point.tangentOut};
		for (uint32_t part = 0; part < 3; ++part) {
			visible[part] = project(handles[part], pixels[part], depths[part]);
			if (!visible[part]) continue;
			draw->AddCircleFilled(pixels[part], part == 0 ? 5.0f : 4.0f, part == 0 ? IM_COL32(255, 210, 65, 255) : IM_COL32(90, 210, 255, 255));
			const float dx = mouse.x - pixels[part].x;
			const float dy = mouse.y - pixels[part].y;
			const float distance = dx * dx + dy * dy;
			if (distance < bestDistance) { bestDistance = distance; hoverPoint = static_cast<int32_t>(i); hoverPart = part; hoverDepth = depths[part]; }
		}
		for (uint32_t part = 1; part < 3; ++part) if (visible[0] && visible[part])
			draw->AddLine(pixels[0], pixels[part], IM_COL32(90, 210, 255, 180), 1);
	}
	draw->PopClipRect();
	const bool inside = mouse.x >= imagePosition.x && mouse.y >= imagePosition.y &&
		mouse.x < imagePosition.x + imageSize.x && mouse.y < imagePosition.y + imageSize.y;
	if (point_ < 0 && inside && hoverPoint >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		world_ = world;
		lifetime_ = world->GetLifetime();
		owner_ = world->GetUUID(entity);
		to_json(before_, *data);
		draft_ = *data;
		point_ = hoverPoint;
		part_ = hoverPart;
		depth_ = hoverDepth;
	}
	const bool captured = point_ >= 0 || (inside && hoverPoint >= 0);
	if (point_ >= 0) {
		auto& point = draft_.points[static_cast<size_t>(point_)];
		const Vector3 ndc((mouse.x - imagePosition.x) / imageSize.x * 2 - 1,
			1 - (mouse.y - imagePosition.y) / imageSize.y * 2, depth_);
		const auto local = Vector3::Transform(Vector3::Transform(ndc, inverseView), inverseWorld);
		if (part_ == 0) point.position = local;
		else if (part_ == 1) point.tangentIn = local - point.position;
		else point.tangentOut = local - point.position;
		SetSpline(*world, entity, draft_);
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			nlohmann::json after;
			to_json(after, draft_);
			point_ = -1;
			if (context.host && before_ != after) context.host->ExecuteEditorCommand(
				std::make_unique<SetSerializedComponentCommand>(entity, "Spline", before_, after));
			world_ = nullptr;
			lifetime_.reset();
		}
	}
	if (captured) context.editorState->useSceneGizmo = true;
	return captured;
}
