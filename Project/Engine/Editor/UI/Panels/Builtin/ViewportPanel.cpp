#include "ViewportPanel.h"
#include <Engine/Editor/UI/ImGui/ImGuiDisplayOutput.h>
#include "ViewportDecalGizmo.h"
#include "ViewportMotionGizmo.h"
#include <Engine/Editor/Utility/LogicPlacementPreview.h>

//============================================================================
//	include
//============================================================================
#include "EditorMeasurementDisplay.h"
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>

// c++
#include <algorithm>
#include <optional>

//============================================================================
//	ViewportPanel classMethods
//============================================================================
namespace {

	// 選択Entityを他のPanelへ渡す
	void DrawViewportEntityDragDropSource(const Engine::EditorPanelContext& context, bool blockByGizmo) {

		if (blockByGizmo || !context.editorState || !context.editorState->enableScenePick || context.editorState->sceneCameraNavigating) {
			return;
		}

		Engine::ECSWorld* world = context.GetWorld();
		if (!world) {
			return;
		}

		// カーソル下のEntityを優先する
		const Engine::Entity dragEntity = world->IsAlive(context.editorState->scenePickDragEntity)
											  ? context.editorState->scenePickDragEntity
											  : context.editorState->selectedEntity;
		if (!world->IsAlive(dragEntity)) {
			return;
		}

		// 他のPanelへ移動してもドラッグを維持する
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

			const Engine::Entity entity = dragEntity;
			const Engine::UUID stableUUID = world->GetUUID(entity);
			ImGui::SetDragDropPayload(Engine::IEditorPanel::kHierarchyDragDropPayloadType, &stableUUID, sizeof(Engine::UUID));

			const std::string displayName = Engine::GetEntityDisplayName(*world, entity);
			ImGui::Text("%s", displayName.c_str());
			ImGui::EndDragDropSource();
		}
	}

}

Engine::ViewportPanel::ViewportPanel(
	const char* windowName, const char* label, ViewportPanelKind kind, TextureUploadService& textureUploadService)
	: toolbar_(textureUploadService), windowName_(windowName), label_(label), kind_(kind) {
}

void Engine::ViewportPanel::Draw(const EditorPanelContext& context) {

	bool* visible = nullptr;
	ImVec2* lastSize = nullptr;
	switch (kind_) {
	case ViewportPanelKind::Scene:

		visible = &context.layoutState->showSceneView;
		lastSize = &context.layoutState->lastSceneViewSize;
		break;
	case ViewportPanelKind::Game:

		visible = &context.layoutState->showGameView;
		lastSize = &context.layoutState->lastGameViewSize;
		break;
	}
	// 非表示のPanelでは未確定操作を終了する
	if (!visible || !*visible) {
		UpdateVisibility(context, false);
		EndPreview();
		return;
	}

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 2.0f));
	const bool drawContents = Engine::EditorWindowPresentation::Begin(MakeWindowName(windowName_).c_str(), visible);
	ImGui::PopStyleVar();
	if (context.host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		context.host->NotifyEditorCommandPanelFocused(EditorCommandPanelKind::Scene);
	}
	if (!drawContents || !*visible) {
		UpdateVisibility(context, false);
		EndPreview();
		ImGui::End();
		return;
	}

	// ビューポートの内容を描画する
	UpdateVisibility(context, true);
	*lastSize = ImGui::GetContentRegionAvail();
	DrawViewportContent(context, label_.c_str(), *lastSize);

	ImGui::End();
}

void Engine::ViewportPanel::UpdateVisibility(const EditorPanelContext& context, bool visible) {

	// 非選択タブと閉じたPanelを描画対象から外す
	if (kind_ == ViewportPanelKind::Scene) {
		context.editorState->sceneViewportVisible = visible;
		context.editorState->sceneViewportHovered = false;
		context.editorState->sceneOrientationHovered = false;
		context.editorState->sceneOrientationActive = false;
		if (!visible) {
			context.editorState->useSceneGizmo = false;
			context.editorState->sceneOrientationMin = context.editorState->sceneOrientationMax = {};
		}
	} else {
		context.editorState->gameViewportVisible = visible;
		context.editorState->gameViewportHovered = false;
	}
	// 表示画像がない場合も古い座標へ操作を送らない
	const InputViewArea inputArea = kind_ == ViewportPanelKind::Game ? InputViewArea::Game : InputViewArea::Scene;
	Input::GetInstance()->ClearViewRect(inputArea);
}

void Engine::ViewportPanel::DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size) {

	ImGui::BeginChild(id, size, ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// 次の描画要求へ実際の表示先を渡す
	ImGuiViewport* viewport = ImGui::GetWindowViewport();
	void* hwnd = viewport->PlatformHandleRaw ? viewport->PlatformHandleRaw : viewport->PlatformHandle;
	if (kind_ == ViewportPanelKind::Game) context.editorState->gameViewportWindow = hwnd;
	else context.editorState->sceneViewportWindow = hwnd;

	// ビューポートの種類に応じた描画ビューのサーフェスを取得
	RenderViewKind viewKind = (kind_ == ViewportPanelKind::Game) ? RenderViewKind::Game : RenderViewKind::Scene;
	InputViewArea inputArea = (kind_ == ViewportPanelKind::Game) ? InputViewArea::Game : InputViewArea::Scene;

	if (const RenderTexture2D* display = context.viewportRenderService->GetDisplayTexture(viewKind)) {

		// 通常画像からデバッグ表示へ切り替える
		D3D12_GPU_DESCRIPTOR_HANDLE imageSRV = display->GetSRVGPUHandle();
		if (context.editorState->gbufferDebugView != GBufferDebugView::None && context.renderPipeline && context.graphicsCore) {

			if (context.editorState->gbufferDebugView == GBufferDebugView::AmbientOcclusion) {

				// AOの遮蔽率を白黒で表示する
				if (auto* ao = context.renderPipeline->GetViewAmbientOcclusionTexture(viewKind)) {
					ao->Transition(*context.graphicsCore->GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					imageSRV = ao->GetGrayscaleSRVGPUHandle();
				} else if (const auto* white = context.graphicsCore->GetBuiltinTextureLibrary().GetWhiteTexture()) {
					imageSRV = white->gpuHandle;
				}
			} else if (context.editorState->gbufferDebugView == GBufferDebugView::Depth) {

				// 深度を線形化した画像を表示する
				if (const RenderTexture2D* depthViz = depthSurface_.RenderDepthVisualization(
						context, viewKind, display->GetRenderTarget().width, display->GetRenderTarget().height)) {

					imageSRV = depthViz->GetSRVGPUHandle();
				}
			} else {

				// GBufferをImGuiの読取状態へ切り替える
				const GBufferAttachment attachment =
					static_cast<GBufferAttachment>(static_cast<uint32_t>(context.editorState->gbufferDebugView) - 1u);
				if (RenderTexture2D* gbuffer = context.renderPipeline->GetViewGBufferTexture(viewKind, attachment)) {

					gbuffer->Transition(
						*context.graphicsCore->GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					imageSRV = gbuffer->GetSRVGPUHandle();
				}
			}
		}

		// Projectの製品画像比率を取得する
		ImVec2 avail = ImGui::GetContentRegionAvail();
		const Vector2I gameSize = EngineContext::GetWindowSetting().gameSize;
		const float aspect = static_cast<float>((std::max)(gameSize.x, 1)) / static_cast<float>((std::max)(gameSize.y, 1));

		// シーン操作を画像の上に一列で表示
		if (kind_ == ViewportPanelKind::Scene) {

			toolbar_.Draw(context);

			// ツール行の高さを除いた画像領域を取得する
			avail = ImGui::GetContentRegionAvail();
		}

		if (avail.x <= 0.0f || avail.y <= 0.0f) { ImGui::EndChild(); return; }

		// 縦横比を保って画像サイズを確定する
		if (avail.x / avail.y >= aspect) {
			viewSize_.y = avail.y;
			viewSize_.x = avail.y * aspect;
		} else {
			viewSize_.x = avail.x;
			viewSize_.y = avail.x / aspect;
		}
		auto& sizing = kind_ == ViewportPanelKind::Scene ? context.editorState->sceneViewportSizing :
			context.editorState->gameViewportSizing;
		const ImVec2 framebufferScale = viewport->FramebufferScale.x > 0.0f ?
			viewport->FramebufferScale : ImGui::GetIO().DisplayFramebufferScale;
		sizing.Update(Vector2(viewSize_.x, viewSize_.y), Vector2(framebufferScale.x, framebufferScale.y),
			gameSize, ImGui::GetTime(), ImGui::IsMouseDown(ImGuiMouseButton_Left));

		ImGui::SetCursorPosX(ImGui::GetCursorPos().x + (avail.x - viewSize_.x) * 0.5f);
		ImGui::SetCursorPosY(ImGui::GetCursorPos().y + (avail.y - viewSize_.y) * 0.5f);

		// 実際にImageを置く位置を入力システムへ渡す
		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		Input::GetInstance()->SetViewRect(inputArea, Vector2(imagePos.x, imagePos.y), Vector2(viewSize_.x, viewSize_.y),
			Vector2(static_cast<float>(gameSize.x), static_cast<float>(gameSize.y)), InputViewCoordinateSpace::Screen);

		// 描画画像を表示する
		if (context.editorState->gbufferDebugView == GBufferDebugView::None) {
			ImGuiDisplayOutput::DrawViewImage(static_cast<ImTextureID>(imageSRV.ptr), viewSize_,
				context.viewportRenderService->GetDisplayOutput(viewKind).paperWhiteNits);
		} else {
			ImGui::Image(static_cast<ImTextureID>(imageSRV.ptr), viewSize_);
		}

		const ViewportOrientationLayout orientation = kind_ == ViewportPanelKind::Scene ?
			ViewportOrientationGizmo::GetLayout(context, imagePos, viewSize_) : ViewportOrientationLayout{};
		const bool overOrientation = orientation.size > 0.0f && ImGui::IsMouseHoveringRect(orientation.position,
			ImVec2(orientation.position.x + orientation.size, orientation.position.y + orientation.size));
		const bool objectEditing = gizmoSession_.IsActive() || splineGizmo_.IsActive();
		const bool blockOrientation = (overOrientation || orientationGizmo_.IsActive()) && !objectEditing &&
			!context.editorState->sceneCameraNavigating;
		const bool imageHovered = ImGui::IsItemHovered() && !blockOrientation;
		if (kind_ == ViewportPanelKind::Scene) {
			context.editorState->sceneOrientationHovered = overOrientation && !objectEditing;
			context.editorState->sceneOrientationActive = orientationGizmo_.IsActive();
			context.editorState->sceneOrientationMin = Vector2(orientation.position.x, orientation.position.y);
			context.editorState->sceneOrientationMax = Vector2(orientation.position.x + orientation.size, orientation.position.y + orientation.size);
		}

		// 他のUIに隠れていない画像のホバーを記録する
		if (context.editorState) {

			if (kind_ == ViewportPanelKind::Scene) {
				context.editorState->sceneViewportHovered = imageHovered;
			} else {
				context.editorState->gameViewportHovered = imageHovered;
			}

			// ダブルクリックで選択EntityへCameraを寄せる
			if (imageHovered && !context.editorState->sceneCameraNavigating && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {

				Engine::ECSWorld* world = context.GetWorld();
				if (world && world->IsAlive(context.editorState->selectedEntity)) {

					const std::optional<Dimension> dimension =
						ResolveEntityDimension(*world, context.editorState->selectedEntity);
					if (dimension && *dimension == Dimension::Type3D) {
						context.editorState->cameraFocusRequest = context.editorState->selectedEntity;
						// Camera移動中のGizmo操作を抑える
						context.editorState->cameraFocusing = true;
					}
				}
			}
		}

		// 画像へのAsset配置を受け付ける
		placementSession_.HandleAssetDropPlacement(context, viewKind, imagePos, gameSize.x,
			gameSize.y, !blockOrientation, viewSize_, kind_ == ViewportPanelKind::Scene);

		// SceneのGizmoとEntityドラッグを描画する
		bool blockDragByGizmo = false;
		if (kind_ == ViewportPanelKind::Scene) {

			DrawLogicPlacementPreview(context, imagePos, viewSize_);
			ViewportDecalGizmo::Draw(context, imagePos, viewSize_);
			ViewportMotionGizmo::Draw(context, imagePos, viewSize_);
			if (!splineGizmo_.Draw(context, imagePos, viewSize_)) gizmoSession_.DrawSceneGizmo(context);
			blockDragByGizmo = context.editorState && context.editorState->useSceneGizmo;
		}
		DrawViewportEntityDragDropSource(context, blockDragByGizmo || blockOrientation);
		if (kind_ == ViewportPanelKind::Scene && context.editorState) {
			if (const auto command = orientationGizmo_.Draw(context, orientation, !objectEditing)) {
				context.host->RequestSceneViewOrientation(*command);
			}
			EditorMeasurementDisplay::DrawOverlay(*context.editorState, imagePos, viewSize_,
				orientation.size > 0.0f ? orientation.size + 8.0f : 0.0f);
		}
	}
	ImGui::EndChild();
}

void Engine::ViewportPanel::EndPreview() {

	orientationGizmo_.Reset();
	gizmoSession_.EndPreview();
	splineGizmo_.EndPreview();
	placementSession_.EndPreview();
}
