#include "ViewportPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTexture2D.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>
#include <Engine/Editor/Utility/JointAttachmentUtility.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Editor/Utility/AssetEntityFactory.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Commands/Entity/CreateDroppedEntityCommand.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/Commands/Transform/SetTransformCommand.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <cmath>
#include <algorithm>
#include <optional>

//============================================================================
//	ViewportPanel classMethods
//============================================================================
namespace {

	// クォータニオンでベクトルを回す、中心ピボットで各位置をorbitさせるために使う

	// シーンギズモの描画に使用するカメラビューを選択する

	// エンティティの親のワールド行列を取得する、ジョイント親子付け中はジョイントを親とみなす

	// グリッド単位へ値を丸める

	// modeと次元に応じたスナップ設定を返す、Noneや非対応はnullptr

	// 絶対スナップ、操作対象のSRT成分を最寄りのグリッドへ丸める

	// HierarchyPanelと同じEntity payloadをViewportからも送る
	void DrawViewportEntityDragDropSource(const Engine::EditorPanelContext& context, bool blockByGizmo) {

		if (blockByGizmo || !context.editorState || !context.editorState->enableScenePick) {
			return;
		}

		Engine::ECSWorld* world = context.GetWorld();
		if (!world) {
			return;
		}

		// ドラッグ中に拾ったカーソル下のエンティティを優先し、無ければ選択中のものをドラッグする
		const Engine::Entity dragEntity =
			world->IsAlive(context.editorState->scenePickDragEntity) ?
			context.editorState->scenePickDragEntity : context.editorState->selectedEntity;
		if (!world->IsAlive(dragEntity)) {
			return;
		}

		// Viewport上ではクリックで選択したEntityを、そのまま他UIへドラッグできるようにする
		// hover中だけに限定すると、ドロップ先へ移動した瞬間にSource描画が切れて"..."表示になる
		if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {

			const Engine::Entity entity = dragEntity;
			const Engine::UUID stableUUID = world->GetUUID(entity);
			ImGui::SetDragDropPayload(Engine::IEditorPanel::kHierarchyDragDropPayloadType, &stableUUID, sizeof(Engine::UUID));

			const std::string displayName = Engine::GetEntityDisplayName(*world, entity);
			ImGui::Text("%s", displayName.c_str());
			ImGui::EndDragDropSource();
		}
	}

	// カメラ選択の候補
	struct CameraChoice {

		Engine::UUID uuid{};
		std::string label;
	};

	// カメラ選択肢のラベルを作る
	std::string MakeCameraLabel(Engine::ECSWorld& world, Engine::Entity entity, const char* suffix) {

		std::string name = Engine::GetEntityDisplayName(world, entity);
		name += " [";
		name += suffix;
		name += "]";
		return name;
	}
	// ワールドからカメラ選択肢を集める
	template <typename TCamera>
	std::vector<CameraChoice> CollectCameraChoices(Engine::ECSWorld& world, const char* suffix) {

		std::vector<CameraChoice> result{};
		// カメラコンポーネントを持つ全てのエンティティに対して処理
		world.ForEach<TCamera>([&](Engine::Entity entity, TCamera& camera) {

			if (!world.IsAlive(entity)) {
				return;
			}
			if (!camera.common.enabled) {
				return;
			}
			if (!IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			CameraChoice choice{};
			choice.uuid = world.GetUUID(entity);
			choice.label = MakeCameraLabel(world, entity, suffix);
			result.push_back(std::move(choice));
			});
		std::sort(result.begin(), result.end(),
			[](const CameraChoice& lhs, const CameraChoice& rhs) {
				return lhs.label < rhs.label;
			});
		return result;
	}
	// カメラ選択肢から現在の選択のプレビュー文字列を見つける
	std::string FindChoicePreview(const std::vector<CameraChoice>& choices,
		Engine::UUID currentUUID, const char* fallbackLabel) {

		if (!currentUUID) {
			return fallbackLabel;
		}
		for (const auto& choice : choices) {
			if (choice.uuid == currentUUID) {
				return choice.label;
			}
		}
		return fallbackLabel;
	}
	// カメラ選択コンボボックスを描画する
	void DrawCameraChoiceCombo(const char* label, Engine::UUID& currentUUID,
		const std::vector<CameraChoice>& choices, const char* autoLabel) {

		std::string preview = FindChoicePreview(choices, currentUUID, autoLabel);

		if (!ImGui::BeginCombo(label, preview.c_str())) {
			return;
		}

		bool autoSelected = !currentUUID;
		if (ImGui::Selectable(autoLabel, autoSelected)) {
			currentUUID = Engine::UUID{};
		}
		if (autoSelected) {
			ImGui::SetItemDefaultFocus();
		}

		for (const auto& choice : choices) {
			const bool isSelected = (choice.uuid == currentUUID);
			if (ImGui::Selectable(choice.label.c_str(), isSelected)) {
				currentUUID = choice.uuid;
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	// ツール間の区切り
	void DrawToolSeparator(const ImVec2& buttonSize) {

		ImGui::Spacing();

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddLine(pos, ImVec2(pos.x + buttonSize.x + buttonSize.x / 2.0f, pos.y), ImGui::GetColorU32(ImGuiCol_Separator));
		ImGui::Dummy(ImVec2(buttonSize.x, 1.0f));

		ImGui::Spacing();
	}
}

Engine::ViewportPanel::ViewportPanel(const char* windowName, const char* label, ViewportPanelKind kind, TextureUploadService& textureUploadService) :
	windowName_(windowName), label_(label), kind_(kind), textureUploadService_(&textureUploadService) {

	icons_.enablePickKey = "enablePickKey.dds";
	icons_.noneKey = "manipulatorNone.png";
	icons_.translateKey = "manipulatorTranslate.png";
	icons_.rotateKey = "manipulatorRotate.png";
	icons_.scaleKey = "manipulatorScale.png";
	icons_.debugCameraKey = "debugCamera.dds";
	icons_.entityCameraKey = "entityCamera.dds";
	icons_.entitySelectKey = "entitySelect.dds";
	icons_.subMeshSelectKey = "subMeshSelect.dds";
	icons_.selection2DKey = "2DOnly.png";
	icons_.selection3DKey = "3DOnly.png";
	icons_.selection2DAnd3DKey = "2DAnd3D.png";
	icons_.drawGridKey = "enabeDrawGrid.png";
	icons_.gizmoCenterPivotKey = "gizmoCenterPivot.png";
	icons_.eachEntityOriginKey = "eachEntityOrigin.png";
	icons_.snapEditEntityKey = "snapEditEntity.png";
	icons_.prefabExitKey = "scene.png";

	// アイコンの読み込み要求
	RequestIcons();
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
	// 無効な種類の場合は何もしない
	if (!visible) {
		return;
	}

	const bool drawContents = ImGui::Begin(windowName_.c_str(), visible);
	if (context.host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
		context.host->NotifyEditorCommandPanelFocused(
			EditorCommandPanelKind::Scene);
	}
	if (!drawContents) {
		ImGui::End();
		return;
	}

	// ビューポートの内容を描画する
	*lastSize = ImGui::GetContentRegionAvail();
	DrawViewportContent(context, label_.c_str(), *lastSize);

	ImGui::End();
}

void Engine::ViewportPanel::DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size) {

	// 描画領域のサイズが小さすぎる場合は、最小サイズを1x1にする
	ImVec2 region = size;

	ImGui::BeginChild(id, region, true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

	// ビューポートの種類に応じた描画ビューのサーフェスを取得
	RenderViewKind viewKind = (kind_ == ViewportPanelKind::Game) ? RenderViewKind::Game : RenderViewKind::Scene;
	InputViewArea inputArea = (kind_ == ViewportPanelKind::Game) ? InputViewArea::Game : InputViewArea::Scene;

	if (const RenderTexture2D* display = context.viewportRenderService->GetDisplayTexture(viewKind)) {

		// プレファブ編集中はアクティブワールドごと隔離ワールドへ切り替わるので、通常のSceneView画像がそのままプレファブを映す
		const RenderTexture2D* shown = display;

		// 通常はshownのSRVを表示する、GBufferデバッグが有効ならそのバッファ/深度のSRVへ差し替える
		D3D12_GPU_DESCRIPTOR_HANDLE imageSRV = shown->GetSRVGPUHandle();
		if (context.editorState->gbufferDebugView != GBufferDebugView::None &&
			context.renderPipeline && context.graphicsCore) {

			if (context.editorState->gbufferDebugView == GBufferDebugView::Depth) {

				// 深度はそのままだと確認しづらいので、線形化グレースケールへ変換した可視化テクスチャを表示する
				if (const RenderTexture2D* depthViz = depthSurface_.RenderDepthVisualization(context, viewKind,
					display->GetRenderTarget().width, display->GetRenderTarget().height)) {

					imageSRV = depthViz->GetSRVGPUHandle();
				}
			} else {

				// 色アタッチメントはImGuiがPixelShaderでサンプルするのでPIXEL_SHADER_RESOURCEへ遷移してから渡す
				// None分だけGBufferAttachmentから+1ずれているので戻す
				const GBufferAttachment attachment = static_cast<GBufferAttachment>(
					static_cast<uint32_t>(context.editorState->gbufferDebugView) - 1u);
				if (RenderTexture2D* gbuffer = context.renderPipeline->GetViewGBufferTexture(viewKind, attachment)) {

					gbuffer->Transition(*context.graphicsCore->GetDXObject().GetDxCommand(),
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
					imageSRV = gbuffer->GetSRVGPUHandle();
				}
			}
		}

		// 表示サイズ
		Vector2 srcSize(static_cast<float>(display->GetRenderTarget().width), static_cast<float>(display->GetRenderTarget().height));

		// 表示ウィンドウの中心に16:9で表示させる
		ImVec2 avail = ImGui::GetContentRegionAvail();
		const float aspect = 16.0f / 9.0f;
		if (avail.x / avail.y >= aspect) {
			viewSize_.y = avail.y;
			viewSize_.x = avail.y * aspect;
		} else {
			viewSize_.x = avail.x;
			viewSize_.y = avail.x / aspect;
		}

		// シーンビューの場合は左側にツールボタンを表示
		if (kind_ == ViewportPanelKind::Scene) {

			ImGui::BeginGroup();

			// プレファブ編集中のみ、ツール列の最上段にIn-Context編集のトグルを置く、デフォルトはオフ
			if (context.editorContext && context.editorContext->isPrefabEditing) {

				const bool inContextActive = context.editorContext->isPrefabInContext;
				if (DrawIconButton("##TogglePrefabInContext", GetTextureID(icons_.prefabExitKey), inContextActive, buttonSize_) &&
					context.host) {
					context.host->RequestTogglePrefabInContext();
				}
				if (ImGui::IsItemHovered()) {

					std::string tooltip = std::string("In-Context編集の切り替え\nオンで元シーンに置いて編集\n現在: ") +
						(inContextActive ? "オン" : "オフ");
					ImGui::SetTooltip("%s", tooltip.c_str());
				}
				DrawToolSeparator(buttonSize_);
			}

			DrawManipulatorSection(context);
			DrawToolSeparator(buttonSize_);
			DrawCameraSection(context);
			DrawToolSeparator(buttonSize_);
			DrawGridSection(context);
			DrawEntityCameraPopup(context);
			ImGui::EndGroup();
			ImGui::SameLine();

			// ツールボタンの幅を除いた残りの領域で中央寄せ
			avail = ImGui::GetContentRegionAvail();
			if (avail.x / avail.y >= aspect) {
				viewSize_.y = avail.y;
				viewSize_.x = avail.y * aspect;
			} else {
				viewSize_.x = avail.x;
				viewSize_.y = avail.x / aspect;
			}
		}

		ImGui::SetCursorPosX(ImGui::GetCursorPos().x + (avail.x - viewSize_.x) * 0.5f);
		ImGui::SetCursorPosY(ImGui::GetCursorPos().y + (avail.y - viewSize_.y) * 0.5f);

		// 実際にImageを置く位置を入力システムへ渡す
		const ImVec2 imagePos = ImGui::GetCursorScreenPos();
		Input::GetInstance()->SetViewRect(inputArea, Vector2(imagePos.x, imagePos.y),
			Vector2(viewSize_.x, viewSize_.y), srcSize,
			InputViewCoordinateSpace::Screen);

		// 描画ビューのサーフェスをImGuiに描画、プレファブ編集中はプレビュー、GBufferデバッグ時はそのバッファを表示する
		ImGui::Image(static_cast<ImTextureID>(imageSRV.ptr), viewSize_);

		// Imageが最前面でホバーされているかを記録する、上に別のImGui/ポップアップがあるとfalseになる
		// この値をピッキング側で参照し、ビューの上に他UIがあるときの誤選択を防ぐ
		if (context.editorState) {

			const bool imageHovered = ImGui::IsItemHovered();
			if (kind_ == ViewportPanelKind::Scene) {
				context.editorState->sceneViewportHovered = imageHovered;
			} else {
				context.editorState->gameViewportHovered = imageHovered;
			}

			// ビューでエンティティをダブルクリックしたらシーンカメラを選択中のエンティティへ寄せる
			if (imageHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {

				Engine::ECSWorld* world = context.GetWorld();
				if (world && world->IsAlive(context.editorState->selectedEntity)) {

					const std::optional<Dimension> dimension = ResolveEntityDimension(
						*world, context.editorState->selectedEntity);
					if (dimension && *dimension == Dimension::Type3D) {
						context.editorState->cameraFocusRequest = context.editorState->selectedEntity;
						// フォーカス開始のこのフレームからギズモを無効にして、ダブルクリックでの誤移動を防ぐ
						context.editorState->cameraFocusing = true;
					}
				}
			}
		}

		// プロジェクトからのアセットのドラッグ&ドロップ配置、Image直後に処理してドロップ対象をImageに対応させる
		placementSession_.HandleAssetDropPlacement(context, viewKind, imagePos,
			display->GetRenderTarget().width, display->GetRenderTarget().height, ImGui::IsItemHovered(), viewSize_, kind_ == ViewportPanelKind::Scene);

		// シーンビューの場合はシーンギズモも描画、フォーカス中はDrawSceneGizmo内で操作を無効化する
		bool blockDragByGizmo = false;
		if (kind_ == ViewportPanelKind::Scene) {

			gizmoSession_.DrawSceneGizmo(context);
			blockDragByGizmo = context.editorState && context.editorState->useSceneGizmo;
		}
		DrawViewportEntityDragDropSource(context, blockDragByGizmo);
	}
	ImGui::EndChild();
}

void Engine::ViewportPanel::DrawSnapSettingsPopup(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	if (!ImGui::BeginPopup("SnapSettingsPopup")) {
		return;
	}

	EntitySnapSettings& settings = context.editorState->snapSettings;

	// ラベル直後にDragを置き、その右へチェックボックスを並べるコンパクトな1行
	auto drawSnapRow = [](const char* id, const char* label, GridSnapAxis& axis, float dragSpeed) {

		ImGui::PushID(id);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(label);
		ImGui::SameLine();

		ImGui::SetNextItemWidth(150.0f);
		ImGui::DragFloat("##size", &axis.size, dragSpeed, 0.0001f, 1000.0f, "%.3f");
		if (axis.size < 0.0001f) {
			axis.size = 0.0001f;
		}
		ImGui::SameLine();
		ImGui::Checkbox("##absolute", &axis.absolute);
		ImGui::PopID();
		};

	ImGui::TextDisabled("スナップ単位 右のチェックで最寄りグリッドへ強制");

	ImGui::SeparatorText("2D");
	drawSnapRow("t2d", "移動", settings.translate2D, 0.01f);
	drawSnapRow("r2d", "回転", settings.rotate2D, 0.1f);
	drawSnapRow("s2d", "拡縮", settings.scale2D, 0.01f);

	ImGui::SeparatorText("3D");
	drawSnapRow("t3d", "移動", settings.translate3D, 0.01f);
	drawSnapRow("r3d", "回転", settings.rotate3D, 0.1f);
	drawSnapRow("s3d", "拡縮", settings.scale3D, 0.01f);

	ImGui::Separator();
	ImGui::Checkbox("スナップグリッドを表示", &settings.drawSnapGrid);

	ImGui::EndPopup();
}

void Engine::ViewportPanel::RequestIcons() {

	if (!textureUploadService_) {
		return;
	}

	textureUploadService_->RequestTextureFile(icons_.enablePickKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.enablePickKey));
	textureUploadService_->RequestTextureFile(icons_.noneKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.noneKey));
	textureUploadService_->RequestTextureFile(icons_.translateKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.translateKey));
	textureUploadService_->RequestTextureFile(icons_.rotateKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.rotateKey));
	textureUploadService_->RequestTextureFile(icons_.scaleKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.scaleKey));
	textureUploadService_->RequestTextureFile(icons_.debugCameraKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.debugCameraKey));
	textureUploadService_->RequestTextureFile(icons_.entityCameraKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.entityCameraKey));
	textureUploadService_->RequestTextureFile(icons_.entitySelectKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.entitySelectKey));
	textureUploadService_->RequestTextureFile(icons_.subMeshSelectKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.subMeshSelectKey));
	textureUploadService_->RequestTextureFile(icons_.selection2DKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.selection2DKey));
	textureUploadService_->RequestTextureFile(icons_.selection3DKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.selection3DKey));
	textureUploadService_->RequestTextureFile(icons_.selection2DAnd3DKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.selection2DAnd3DKey));
	textureUploadService_->RequestTextureFile(icons_.drawGridKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.drawGridKey));
	textureUploadService_->RequestTextureFile(icons_.gizmoCenterPivotKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.gizmoCenterPivotKey));
	textureUploadService_->RequestTextureFile(icons_.eachEntityOriginKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.eachEntityOriginKey));
	textureUploadService_->RequestTextureFile(icons_.snapEditEntityKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.snapEditEntityKey));
	textureUploadService_->RequestTextureFile(icons_.prefabExitKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.prefabExitKey));
}

ImTextureID Engine::ViewportPanel::GetTextureID(const std::string& key) const {

	if (!textureUploadService_) {
		return ImTextureID{};
	}
	return EditorTextureHelper::GetImTextureID(*textureUploadService_, key);
}

void Engine::ViewportPanel::DrawCameraSection(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	SceneViewCameraSelection& selection = context.editorState->sceneViewCamera;

	// デバッグカメラを選ぶボタン
	if (DrawIconButton("##SceneCameraDebug", GetTextureID(icons_.debugCameraKey),
		selection.mode == SceneViewCameraMode::DebugManual, buttonSize_)) {

		selection.mode = SceneViewCameraMode::DebugManual;
		selection.ClearAssignedCameras();
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("マニュアルカメラ操作");
	}
	// 選択エンティティのカメラを選ぶボタン
	if (DrawIconButton("##SceneCameraSelected", GetTextureID(icons_.entityCameraKey),
		selection.mode == SceneViewCameraMode::SelectedEntityCamera, buttonSize_)) {

		selection.mode = SceneViewCameraMode::SelectedEntityCamera;
		ImGui::OpenPopup("##SceneEntityCameraPopup");
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("エンティティカメラによる制御");
	}
}

void Engine::ViewportPanel::DrawManipulatorSection(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	SceneViewManipulatorMode& mode = context.editorState->sceneViewManipulatorMode;
	// エンティティピックの有効/無効
	{
		if (DrawIconButton("##EnablePickKey", GetTextureID(icons_.enablePickKey),
			!context.editorState->enableScenePick, buttonSize_)) {

			context.editorState->enableScenePick = !context.editorState->enableScenePick;
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("シーンオブジェクト選択の有効/無効切り替え\n現在の状態: ") +
				(context.editorState->enableScenePick ? "有効" : "無効");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}

		// View上で選択できるエンティティ次元を3D、2D、両方の順で切り替える
		SceneViewPickDimension& pickDimension = context.editorState->sceneViewPickDimension;
		ImTextureID dimensionIcon = GetTextureID(icons_.selection3DKey);
		const char* dimensionLabel = "3Dのみ";
		if (pickDimension == SceneViewPickDimension::Type2D) {
			dimensionIcon = GetTextureID(icons_.selection2DKey);
			dimensionLabel = "2Dのみ";
		} else if (pickDimension == SceneViewPickDimension::Both) {
			dimensionIcon = GetTextureID(icons_.selection2DAnd3DKey);
			dimensionLabel = "2D・3D";
		}
		if (DrawIconButton("##ScenePickDimension", dimensionIcon, true, buttonSize_)) {

			switch (pickDimension) {
			case SceneViewPickDimension::Type3D:
				pickDimension = SceneViewPickDimension::Type2D;
				break;
			case SceneViewPickDimension::Type2D:
				pickDimension = SceneViewPickDimension::Both;
				break;
			case SceneViewPickDimension::Both:
				pickDimension = SceneViewPickDimension::Type3D;
				break;
			}
		}
		if (ImGui::IsItemHovered()) {

			if (pickDimension == SceneViewPickDimension::Type2D) {
				dimensionLabel = "2Dのみ";
			} else if (pickDimension == SceneViewPickDimension::Both) {
				dimensionLabel = "2D・3D";
			} else {
				dimensionLabel = "3Dのみ";
			}
			const std::string tooltip = std::string("選択できるエンティティ次元\n現在の状態: ") +
				dimensionLabel;
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
		if (DrawIconButton("##ManipulatorNone", GetTextureID(icons_.noneKey),
			mode == SceneViewManipulatorMode::None, buttonSize_)) {

			mode = SceneViewManipulatorMode::None;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("マニュピレーター表示なし H");
		}
	}
	// エンティティ/サブメッシュ選択モードの切り替え
	{
		EditorSelectionKind& kind = context.editorState->selectKind;
		if (kind == EditorSelectionKind::Entity) {
			if (DrawIconButton("##SelectEntity", GetTextureID(icons_.entitySelectKey), true, buttonSize_)) {

				kind = EditorSelectionKind::MeshSubMesh;
			}
		} else {
			if (DrawIconButton("##SelectSubMesh", GetTextureID(icons_.subMeshSelectKey), true, buttonSize_)) {

				kind = EditorSelectionKind::Entity;
			}
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("選択対象の切り替え\n現在の対象: ") +
				(kind == EditorSelectionKind::Entity ? "エンティティ単位" : "サブメッシュ単位 E");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
	}
	DrawToolSeparator(buttonSize_);
	// SRT編集
	{
		if (DrawIconButton("##ManipulatorTranslate", GetTextureID(icons_.translateKey),
			mode == SceneViewManipulatorMode::Translate, buttonSize_)) {

			mode = SceneViewManipulatorMode::Translate;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("座標編集 T");
		}
		if (DrawIconButton("##ManipulatorRotate", GetTextureID(icons_.rotateKey),
			mode == SceneViewManipulatorMode::Rotate, buttonSize_)) {

			mode = SceneViewManipulatorMode::Rotate;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("回転編集 R");
		}
		if (DrawIconButton("##ManipulatorScale", GetTextureID(icons_.scaleKey),
			mode == SceneViewManipulatorMode::Scale, buttonSize_)) {

			mode = SceneViewManipulatorMode::Scale;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("拡縮編集 S");
		}
	}
	DrawToolSeparator(buttonSize_);
	// オブジェクトのスナップ操作の有効/無効
	{
		if (DrawIconButton("##EnableSnapEntity", GetTextureID(icons_.snapEditEntityKey),
			context.editorState->enableSnapEditEntity, buttonSize_)) {

			context.editorState->enableSnapEditEntity = !context.editorState->enableSnapEditEntity;
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("エンティティスナップ操作の有効/無効切り替え\n左クリックで切替 右クリックで設定\n現在の状態: ") +
				(context.editorState->enableSnapEditEntity ? "有効" : "無効 G");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
		// 右クリックでスナップ単位の調整ポップアップを開く
		ImGui::OpenPopupOnItemClick("SnapSettingsPopup", ImGuiPopupFlags_MouseButtonRight);
		DrawSnapSettingsPopup(context);
	}
	// 複数選択ギズモのピボット切り替え、現在のモードのアイコンを表示する
	{
		const std::string& pivotIcon = context.editorState->gizmoPivotAtCenter ?
			icons_.gizmoCenterPivotKey : icons_.eachEntityOriginKey;
		if (DrawIconButton("##GizmoPivotMode", GetTextureID(pivotIcon), true, buttonSize_)) {

			context.editorState->gizmoPivotAtCenter = !context.editorState->gizmoPivotAtCenter;
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("複数選択ギズモのピボット\n現在: ") +
				(context.editorState->gizmoPivotAtCenter ? "選択中心" : "各エンティティ原点");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
	}
}

void Engine::ViewportPanel::DrawGridSection(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	if (DrawIconButton("##SceneViewDefaultGrid", GetTextureID(icons_.drawGridKey),
		context.editorState->drawSceneViewDefaultGrid, buttonSize_)) {

		context.editorState->drawSceneViewDefaultGrid = !context.editorState->drawSceneViewDefaultGrid;
	}
	if (ImGui::IsItemHovered()) {

		std::string tooltip = std::string("SceneViewグリッド表示\n現在の状態: ") +
			(context.editorState->drawSceneViewDefaultGrid ? "有効" : "無効");
		ImGui::SetTooltip("%s", tooltip.c_str());
	}
}

bool Engine::ViewportPanel::DrawIconButton(const char* id, ImTextureID textureID, bool active, const ImVec2& size) const {

	const ImVec4 normal = active
		? ImVec4(0.05f, 0.18f, 0.45f, 1.00f)   // active: dark deep blue
		: ImVec4(0.04f, 0.04f, 0.04f, 0.95f);  // inactive: near black

	const ImVec4 hovered = active
		? ImVec4(0.07f, 0.24f, 0.58f, 1.00f)   // active hover: slightly brighter dark blue
		: ImVec4(0.08f, 0.08f, 0.08f, 0.98f);  // inactive hover

	const ImVec4 pressed = active
		? ImVec4(0.10f, 0.32f, 0.74f, 1.00f)   // active pressed: stronger blue
		: ImVec4(0.02f, 0.02f, 0.02f, 1.00f);  // inactive pressed

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
	ImGui::PushStyleColor(ImGuiCol_Button, normal);
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hovered);
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, pressed);

	bool result = ImGui::ImageButton(id, textureID, size, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
		ImVec4(0.0f, 0.0f, 0.0f, 0.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

	ImGui::PopStyleColor(3);
	ImGui::PopStyleVar();

	if (active) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
			IM_COL32(26, 82, 190, 255), 4.0f, 0, 2.0f);
	}
	return result;
}

void Engine::ViewportPanel::DrawEntityCameraPopup(const EditorPanelContext& context) {

	if (!context.editorState) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return;
	}

	SceneViewCameraSelection& selection = context.editorState->sceneViewCamera;
	std::vector<CameraChoice> orthoChoices = CollectCameraChoices<OrthographicCameraComponent>(*world, "2D");
	std::vector<CameraChoice> perspChoices = CollectCameraChoices<PerspectiveCameraComponent>(*world, "3D");

	if (!ImGui::BeginPopup("##SceneEntityCameraPopup")) {
		return;
	}

	ImGui::PushItemWidth(256.0f);
	DrawCameraChoiceCombo("2D カメラ", selection.orthographicCameraUUID, orthoChoices, "<Auto 2D>");
	DrawCameraChoiceCombo("3D カメラ", selection.perspectiveCameraUUID, perspChoices, "<Auto 3D>");
	ImGui::PopItemWidth();

	ImGui::Separator();

	if (ImGui::Button("選択クリア")) {

		selection.ClearAssignedCameras();
	}
	ImGui::SameLine();
	if (ImGui::Button("デバッグカメラに戻す")) {

		selection.mode = SceneViewCameraMode::DebugManual;
		selection.ClearAssignedCameras();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}
