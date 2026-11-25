#include "ViewportOrientationGizmo.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/EditorPalette.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpersInternal.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>

namespace {

	struct AxisItem {

		Engine::Vector3 direction;
		ImVec2 position;
		float depth;
		int index;
	};
	constexpr const char* kAxisColors[]{"Guides.axisX", "Guides.axisY", "Guides.axisZ"};
	constexpr const char* kAxisNames[]{"X", "Y", "Z"};
}

ImU32 Engine::ViewportOrientationGizmo::GetAxisColor(int axis) {

	return EditorPalette::U32(kAxisColors[axis]);
}

Engine::ViewportOrientationLayout Engine::ViewportOrientationGizmo::GetLayout(const EditorPanelContext& context,
	const ImVec2& position, const ImVec2& size) {

	if (!context.editorState || !context.editorState->drawSceneViewOrientation ||
		context.editorState->sceneViewPickDimension == SceneViewPickDimension::Type2D) return {};
	const float diameter = (std::min)(ImGui::GetFontSize() * 3.25f, (std::min)(size.x, size.y) - 16.0f);
	if (diameter < ImGui::GetFontSize() * 2.0f) return {};
	return {ImVec2(position.x + size.x - diameter - 8.0f, position.y + 8.0f), diameter};
}

std::optional<Engine::SceneViewCameraGizmoCommand> Engine::ViewportOrientationGizmo::Draw(const EditorPanelContext& context,
	const ViewportOrientationLayout& layout, bool allowInteraction) {

	if (layout.size <= 0 || !context.sceneRenderView) { Reset(); return std::nullopt; }
	const auto* camera = context.sceneRenderView->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) { Reset(); return std::nullopt; }
	std::optional<SceneViewCameraGizmoCommand> command;
	const bool enabled = allowInteraction && !context.editorState->sceneCameraNavigating &&
		context.editorState->sceneViewCamera.mode == SceneViewCameraMode::DebugManual;
	const float scale = layout.size / 100.0f;
	const float radius = 9.0f * scale;
	const ImVec2 center(layout.position.x + layout.size * 0.5f, layout.position.y + layout.size * 0.5f);
	std::array<AxisItem, 6> axes;
	for (int index = 0; index < 6; ++index) {
		Vector3 direction{};
		if (index / 2 == 0) direction.x = index % 2 ? -1.0f : 1.0f;
		else if (index / 2 == 1) direction.y = index % 2 ? -1.0f : 1.0f;
		else direction.z = index % 2 ? -1.0f : 1.0f;
		const Vector3 view = Vector3::TransferNormal(direction, camera->matrices.viewMatrix);
		axes[index] = {direction, ImVec2(center.x + view.x * 34.0f * scale, center.y - view.y * 34.0f * scale), view.z, index};
	}
	std::sort(axes.begin(), axes.end(), [](const auto& left, const auto& right) {
		return left.depth == right.depth ? left.index < right.index : left.depth > right.depth;
	});
	const ImVec2 mouse = ImGui::GetMousePos();
	int hovered = -1;
	for (const auto& axis : axes) {
		const float x = mouse.x - axis.position.x, y = mouse.y - axis.position.y;
		if (x * x + y * y <= radius * radius) hovered = axis.index;
	}
	ImGui::BeginDisabled(!enabled);
	DrawOverlayButton("SceneOrientation", layout.position, ImVec2(layout.size, layout.size));
	const bool itemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
	if (!itemHovered) hovered = -1;
	if (ImGui::IsItemActivated()) {
		pressed_ = true;
		dragged_ = false;
		pressedAxis_ = hovered;
		pressMouse_ = lastMouse_ = mouse;
	}
	if (pressed_ && enabled && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
		const float x = mouse.x - pressMouse_.x, y = mouse.y - pressMouse_.y;
		const float threshold = ImGui::GetIO().MouseDragThreshold;
		if (dragged_ || x * x + y * y >= threshold * threshold) {
			dragged_ = true;
			const Vector2 delta(mouse.x - lastMouse_.x, mouse.y - lastMouse_.y);
			if (delta.x != 0 || delta.y != 0) command = SceneViewCameraGizmoCommand{
				.action = SceneViewCameraGizmoAction::Orbit, .delta = delta};
			lastMouse_ = mouse;
		}
		if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (!dragged_ && pressedAxis_ >= 0 && hovered == pressedAxis_) {
				const auto axis = std::find_if(axes.begin(), axes.end(), [&](const auto& value) { return value.index == pressedAxis_; });
				command = SceneViewCameraGizmoCommand{.action = SceneViewCameraGizmoAction::Align, .axis = axis->direction};
			}
			Reset();
		} else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) Reset();
	} else Reset();
	ImGui::EndDisabled();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(layout.position, ImVec2(layout.position.x + layout.size, layout.position.y + layout.size), true);
	if (itemHovered || pressed_) draw->AddCircleFilled(center, layout.size * 0.49f, Engine::EditorPalette::U32("Guides.orientationBackdrop"));
	for (const auto& axis : axes) {
		ImVec4 color = ImGui::ColorConvertU32ToFloat4(GetAxisColor(axis.index / 2));
		if (!enabled) color.w *= ImGui::GetStyle().DisabledAlpha;
		const ImU32 axisColor = ImGui::ColorConvertFloat4ToU32(color);
		const bool positive = axis.index % 2 == 0;
		if (positive) draw->AddLine(center, axis.position, axisColor, 2.0f * scale);
		if (positive || hovered == axis.index) draw->AddCircleFilled(axis.position, radius, axisColor);
		else {
			draw->AddCircleFilled(axis.position, radius, Engine::EditorPalette::U32("Guides.orientationNegative"));
			draw->AddCircle(axis.position, radius, axisColor, 0, 1.5f * scale);
		}
		if (hovered == axis.index) draw->AddCircle(axis.position, radius + scale, Engine::EditorPalette::U32("Guides.orientationHover"), 0, 1.5f * scale);
		if (positive) {
			const char* text = kAxisNames[axis.index / 2];
			ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.5f);
			const ImVec2 textSize = ImGui::CalcTextSize(text);
			draw->AddText(ImVec2(axis.position.x - textSize.x * 0.5f, axis.position.y - textSize.y * 0.5f), Engine::EditorPalette::U32("Guides.orientationText"), text);
			ImGui::PopFont();
		}
	}
	draw->PopClipRect();
	context.editorState->sceneOrientationActive = pressed_;
	return command;
}
