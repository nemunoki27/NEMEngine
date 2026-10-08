#include "ViewportPanel.h"

//============================================================================
//	include
//============================================================================
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

		if (blockByGizmo || !context.editorState || !context.editorState->enableScenePick) {
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
		EndPreview();
		return;
	}

	const bool drawContents = ImGui::Begin(windowName_.c_str(), visible);
	if (context.host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		context.host->NotifyEditorCommandPanelFocused(EditorCommandPanelKind::Scene);
	}
	if (!drawContents || !*visible) {
		EndPreview();
		ImGui::End();
		return;
	}

	// ビューポートの内容を描画する
	*lastSize = ImGui::GetContentRegionAvail();
	DrawViewportContent(context, label_.c_str(), *lastSize);

	ImGui::End();
}

void Engine::ViewportPanel::DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size) {

	ImGui::BeginChild(id, size, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// ビューポートの種類に応じた描画ビューのサーフェスを取得
	RenderViewKind viewKind = (kind_ == ViewportPanelKind::Game) ? RenderViewKind::Game : RenderViewKind::Scene;
	InputViewArea inputArea = (kind_ == ViewportPanelKind::Game) ? InputViewArea::Game : InputViewArea::Scene;

	if (const RenderTexture2D* display = context.viewportRenderService->GetDisplayTexture(viewKind)) {

		// 通常画像からデバッグ表示へ切り替える
		D3D12_GPU_DESCRIPTOR_HANDLE imageSRV = display->GetSRVGPUHandle();
		if (context.editorState->gbufferDebugView != GBufferDebugView::None && context.renderPipeline && context.graphicsCore) {

			if (context.editorState->gbufferDebugView == GBufferDebugView::Depth) {

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

		// 表示サイズ
		Vector2 srcSize(
			static_cast<float>(display->GetRenderTarget().width), static_cast<float>(display->GetRenderTarget().height));

		// Projectの製品画像比率を取得する
		ImVec2 avail = ImGui::GetContentRegionAvail();
		const Vector2I gameSize = EngineContext::GetWindowSetting().gameSize;
		const float aspect = static_cast<float>((std::max)(gameSize.x, 1)) / static_cast<float>((std::max)(gameSize.y, 1));

		// シーンビューの場合は左側にツールボタンを表示
		if (kind_ == ViewportPanelKind::Scene) {

			toolbar_.Draw(context);
			ImGui::SameLine();

			// ツール列の幅を除いた画像領域を取得する
			avail = ImGui::GetContentRegionAvail();
		}

		// 縦横比を保って画像サイズを確定する
		if (avail.x / avail.y >= aspect) {
			viewSize_.y = avail.y;
			viewSize_.x = avail.y * aspect;
		} else {
			viewSize_.x = avail.x;
			viewSize_.y = avail.x / aspect;
		}

		ImGui::SetCursorPosX(ImGui::GetCursorPos().x + (avail.x - viewSize_.x) * 0.5f);
		ImGui::SetCursorPosY(ImGui::GetCursorPos().y + (avail.y - viewSize_.y) * 0.5f);

		// 実際にImageを置く位置を入力システムへ渡す
		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		Input::GetInstance()->SetViewRect(inputArea, Vector2(imagePos.x, imagePos.y), Vector2(viewSize_.x, viewSize_.y),
			srcSize, InputViewCoordinateSpace::Screen);

		// 描画画像を表示する
		ImGui::Image(static_cast<ImTextureID>(imageSRV.ptr), viewSize_);

		// 他のUIに隠れていない画像のホバーを記録する
		if (context.editorState) {

			const bool imageHovered = ImGui::IsItemHovered();
			if (kind_ == ViewportPanelKind::Scene) {
				context.editorState->sceneViewportHovered = imageHovered;
			} else {
				context.editorState->gameViewportHovered = imageHovered;
			}

			// ダブルクリックで選択EntityへCameraを寄せる
			if (imageHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {

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
		placementSession_.HandleAssetDropPlacement(context, viewKind, imagePos, display->GetRenderTarget().width,
			display->GetRenderTarget().height, ImGui::IsItemHovered(), viewSize_, kind_ == ViewportPanelKind::Scene);

		// SceneのGizmoとEntityドラッグを描画する
		bool blockDragByGizmo = false;
		if (kind_ == ViewportPanelKind::Scene) {

			gizmoSession_.DrawSceneGizmo(context);
			blockDragByGizmo = context.editorState && context.editorState->useSceneGizmo;
		}
		DrawViewportEntityDragDropSource(context, blockDragByGizmo);
	}
	ImGui::EndChild();
}

void Engine::ViewportPanel::EndPreview() {

	gizmoSession_.EndPreview();
	gizmoSession_.EndPreview();
	placementSession_.EndPreview();
}
