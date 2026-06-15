#include "ViewportPanel.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
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
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/Commands/Transform/SetTransformCommand.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

//============================================================================
//	ViewportPanel classMethods
//============================================================================
namespace {

	// クォータニオンでベクトルを回す、中心ピボットで各位置をorbitさせるために使う
	Engine::Vector3 RotateVectorByQuaternion(const Engine::Quaternion& q, const Engine::Vector3& v) {

		const Engine::Vector3 axis(q.x, q.y, q.z);
		const Engine::Vector3 t = Engine::Vector3::Cross(axis, v) * 2.0f;
		return v + t * q.w + Engine::Vector3::Cross(axis, t);
	}

	bool Prefers2DGizmo(const Engine::EditorPanelContext& context,
		Engine::ECSWorld& world, const Engine::Entity& entity) {

		// Textは2D/3D両対応なのでdimensionで判定する
		if (world.HasComponent<Engine::TextRendererComponent>(entity)) {
			return world.GetComponent<Engine::TextRendererComponent>(entity).dimension == Engine::Dimension::Type2D;
		}
		// 2D描画に関係するコンポーネントがある場合は2Dギズモを優先
		if (world.HasComponent<Engine::SpriteRendererComponent>(entity) ||
			world.HasComponent<Engine::OrthographicCameraComponent>(entity)) {
			return true;
		}
		if (world.HasComponent<Engine::MeshRendererComponent>(entity) ||
			world.HasComponent<Engine::PerspectiveCameraComponent>(entity) ||
			world.HasComponent<Engine::DirectionalLightComponent>(entity) ||
			world.HasComponent<Engine::PointLightComponent>(entity) ||
			world.HasComponent<Engine::SpotLightComponent>(entity)) {
			return false;
		}
		return context.editorState && context.editorState->manualCameraDimension == Engine::Dimension::Type2D;
	}
	// シーンギズモの描画に使用するカメラビューを選択する
	const Engine::ResolvedCameraView* SelectSceneGizmoCamera(const Engine::ResolvedRenderView& view, bool prefer2DTarget) {

		if (prefer2DTarget) {
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
			if (view.perspective.valid) {
				return &view.perspective;
			}
		} else {
			if (view.perspective.valid) {
				return &view.perspective;
			}
			if (view.orthographic.valid) {
				return &view.orthographic;
			}
		}
		return nullptr;
	}
	// エンティティの親のワールド行列を取得する
	Engine::Matrix4x4 GetEntityParentWorldMatrix(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return Engine::Matrix4x4::Identity();
		}
		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		if (!world.IsAlive(hierarchy.parent) || !world.HasComponent<Engine::TransformComponent>(hierarchy.parent)) {

			return Engine::Matrix4x4::Identity();
		}
		return world.GetComponent<Engine::TransformComponent>(hierarchy.parent).worldMatrix;
	}
	// グリッド単位へ値を丸める
	float SnapValueToGrid(float value, float grid) {

		return grid > 0.0f ? std::round(value / grid) * grid : value;
	}
	// modeと次元に応じたスナップ設定を返す、Noneや非対応はnullptr
	const Engine::GridSnapAxis* SelectSnapAxis(const Engine::EntitySnapSettings& settings,
		Engine::SceneViewManipulatorMode mode, bool use2D) {

		switch (mode) {
		case Engine::SceneViewManipulatorMode::Translate: return use2D ? &settings.translate2D : &settings.translate3D;
		case Engine::SceneViewManipulatorMode::Rotate:    return use2D ? &settings.rotate2D : &settings.rotate3D;
		case Engine::SceneViewManipulatorMode::Scale:     return use2D ? &settings.scale2D : &settings.scale3D;
		default: return nullptr;
		}
	}
	// 絶対スナップ、操作対象のSRT成分を最寄りのグリッドへ丸める
	void ApplyAbsoluteSnap(Engine::TransformComponent& transform,
		Engine::SceneViewManipulatorMode mode, float grid) {

		switch (mode) {
		case Engine::SceneViewManipulatorMode::Translate:
			transform.localPos.x = SnapValueToGrid(transform.localPos.x, grid);
			transform.localPos.y = SnapValueToGrid(transform.localPos.y, grid);
			transform.localPos.z = SnapValueToGrid(transform.localPos.z, grid);
			break;
		case Engine::SceneViewManipulatorMode::Scale:
			transform.localScale.x = SnapValueToGrid(transform.localScale.x, grid);
			transform.localScale.y = SnapValueToGrid(transform.localScale.y, grid);
			transform.localScale.z = SnapValueToGrid(transform.localScale.z, grid);
			break;
		case Engine::SceneViewManipulatorMode::Rotate: {
			// 回転は一度Euler角へ落としてから丸めて戻す
			Engine::Vector3 euler = Engine::Quaternion::ToEulerAngles(transform.localRotation);
			euler.x = SnapValueToGrid(euler.x, grid);
			euler.y = SnapValueToGrid(euler.y, grid);
			euler.z = SnapValueToGrid(euler.z, grid);
			transform.localRotation = Engine::Quaternion::Normalize(Engine::Quaternion::EulerToQuaternion(euler));
			break;
		}
		default:
			break;
		}
	}
	// HierarchyPanelと同じEntity payloadをViewportからも送る
	void DrawViewportEntityDragDropSource(const Engine::EditorPanelContext& context, bool blockByGizmo) {

		if (blockByGizmo || !context.editorState || !context.editorState->enableScenePick) {
			return;
		}

		Engine::ECSWorld* world = context.GetWorld();
		if (!world) {
			return;
		}

		// Ctrl併用時は選択を変えずカーソル下から拾ったエンティティをドラッグ対象にする
		const bool ctrlHeld = ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl);
		const Engine::Entity dragEntity =
			(ctrlHeld && world->IsAlive(context.editorState->scenePickDragEntity)) ?
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

			std::string displayName = "Entity";
			if (world->HasComponent<Engine::NameComponent>(entity)) {
				displayName = world->GetComponent<Engine::NameComponent>(entity).name;
			}
			if (displayName.empty()) {
				displayName = "Entity";
			}
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

		std::string name = "Entity";
		if (world.HasComponent<Engine::NameComponent>(entity)) {
			name = world.GetComponent<Engine::NameComponent>(entity).name;
		}
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
	icons_.manualCamera2DKey = "sceneCameraMode2D.dds";
	icons_.manualCamera3DKey = "sceneCameraMode3D.dds";
	icons_.drawGridKey = "enabeDrawGrid.png";
	icons_.gizmoCenterPivotKey = "gizmoCenterPivot.png";
	icons_.eachEntityOriginKey = "eachEntityOrigin.png";
	icons_.snapEditEntityKey = "snapEditEntity.png";

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

	if (!ImGui::Begin(windowName_.c_str(), visible)) {
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

		// プレファブ編集中はSceneViewの画像を編集インスタンスのプレビューへ切り替える
		const RenderTexture2D* shown = display;
		if (const RenderTexture2D* preview = RenderPrefabEditPreview(context,
			display->GetRenderTarget().width, display->GetRenderTarget().height)) {

			shown = preview;
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
			DrawManipulatorSection(context);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			DrawCameraSection(context);
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
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
			Vector2(viewSize_.x, viewSize_.y), srcSize);

		// 描画ビューのサーフェスをImGuiに描画、プレファブ編集中はプレビュー画像を表示する
		ImGui::Image(static_cast<ImTextureID>(shown->GetSRVGPUHandle().ptr), viewSize_);

		// Imageが最前面でホバーされているかを記録する、上に別のImGui/ポップアップがあるとfalseになる
		// この値をピッキング側で参照し、ビューの上に他UIがあるときの誤選択を防ぐ
		if (context.editorState) {

			const bool imageHovered = ImGui::IsItemHovered();
			if (kind_ == ViewportPanelKind::Scene) {
				context.editorState->sceneViewportHovered = imageHovered;
			} else {
				context.editorState->gameViewportHovered = imageHovered;
			}
		}

		// シーンビューの場合はシーンギズモも描画
		bool blockDragByGizmo = false;
		if (kind_ == ViewportPanelKind::Scene) {

			DrawSceneGizmo(context);
			blockDragByGizmo = context.editorState && context.editorState->useSceneGizmo;
		}
		DrawViewportEntityDragDropSource(context, blockDragByGizmo);
	}
	ImGui::EndChild();
}

const Engine::RenderTexture2D* Engine::ViewportPanel::RenderPrefabEditPreview(
	const EditorPanelContext& context, uint32_t width, uint32_t height) {

	// SceneViewのみが対象、編集中でなければ通常のSceneView画像へ戻す
	if (kind_ != ViewportPanelKind::Scene || !context.editorState ||
		!context.graphicsCore || !context.renderPipeline || !context.editorContext) {
		return nullptr;
	}
	ECSWorld* world = context.GetWorld();
	const Entity instance = context.editorState->prefabEditInstance;
	AssetDatabase* assetDatabase = context.editorContext->assetDatabase;
	if (!instance.IsValid() || !world || !world->IsAlive(instance) || !assetDatabase ||
		width == 0 || height == 0) {
		return nullptr;
	}

	// サイズが変わったら描画先を作り直す
	if (!prefabPreviewSurface_ || prefabPreviewWidth_ != width || prefabPreviewHeight_ != height) {

		prefabPreviewSurface_ = std::make_unique<MultiRenderTarget>();

		// SceneViewと同じ構成のサーフェスを作る、色はHDR、深度はSceneViewと同形式
		MultiRenderTargetCreateDesc desc{};
		desc.width = width;
		desc.height = height;
		ColorAttachmentDesc color{};
		color.name = "Preview.Color";
		color.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		color.clearColor = Color4::FromHex(0x306030ff);
		color.createUAV = false;
		desc.colors.emplace_back(color);
		DepthTextureCreateDesc depth{};
		depth.width = width;
		depth.height = height;
		depth.resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
		depth.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depth.srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		depth.debugName = L"PrefabPreviewDepth";
		desc.depth = depth;

		prefabPreviewSurface_->Create(context.graphicsCore->GetDXObject().GetDevice(),
			&context.graphicsCore->GetRTVDescriptor(), &context.graphicsCore->GetDSVDescriptor(),
			&context.graphicsCore->GetSRVDescriptor(), desc);
		prefabPreviewWidth_ = width;
		prefabPreviewHeight_ = height;
	}
	if (!prefabPreviewSurface_->IsValid()) {
		return nullptr;
	}

	// 既存のSceneViewカメラで、編集インスタンスだけをプレビューサーフェスへ描画する
	EntityPreviewRenderRequest request{};
	request.world = world;
	request.assetDatabase = assetDatabase;
	request.sceneHeader = context.editorContext->activeSceneHeader;
	request.sceneInstanceID = context.editorContext->activeSceneInstanceID;
	request.rootEntity = instance;
	request.surface = prefabPreviewSurface_.get();
	request.camera = context.sceneViewCamera;
	request.clearSurface = true;
	request.drawGrid3D = context.editorState->drawSceneViewDefaultGrid;
	if (!context.renderPipeline->RenderEntityPreview(*context.graphicsCore, request)) {
		return nullptr;
	}

	return prefabPreviewSurface_->GetColorTexture(0);
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
	// BeginPopupはAlwaysAutoResizeなのでDragは固定幅にする、可変幅だと循環依存で極小化する
	// 絶対スナップのチェックがtrueなら、操作結果を最寄りグリッドへ強制する
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

void Engine::ViewportPanel::DrawSceneGizmo(const EditorPanelContext& context) {

	// フラグリセット
	context.editorState->useSceneGizmo = false;

	ECSWorld* worldPtr = context.GetWorld();
	if (!worldPtr) {
		return;
	}
	ECSWorld& world = *worldPtr;

	// 編集不可の場合はギズモセッションを終了して何もしない
	if (!context.CanEditScene() || !context.sceneRenderView || !context.sceneRenderView->valid ||
		context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}
	// シーンビューのマニピュレーター選択が「なし」の場合はギズモセッションを終了して何もしない
	if (context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	// 現在のマニピュレーター操作を取得
	const ImVec2 rectMin = ImGui::GetItemRectMin();
	const ImVec2 rectMax = ImGui::GetItemRectMax();

	// ギズモの描画に必要な情報をまとめた構造体を作成
	GizmoViewportRect rect{};
	rect.x = rectMin.x;
	rect.y = rectMin.y;
	rect.width = rectMax.x - rectMin.x;
	rect.height = rectMax.y - rectMin.y;
	// 描画領域が有効でない場合はギズモセッションを終了して何もしない
	if (!rect.IsValid()) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	//============================================================================
	//	エンティティ選択中
	//============================================================================
	{
		// 複数選択中は中心ピボットで各エンティティを個別原点で動かすギズモへ切り替える
		if (context.editorState->SelectionCount() > 1) {

			FinalizeEntityGizmoSession(context, world);
			DrawMultiEntityGizmo(context, world, rect);
			return;
		}
		FinalizeMultiEntityGizmoSession(context, world);

		const Entity entity = context.editorState->selectedEntity;
		// 編集不可なエンティティの場合はギズモセッションを終了して何もしない
		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			FinalizeEntityGizmoSession(context, world);
			return;
		}
		// 別セッションなら先に閉じる
		if (entityGizmoSession_.active && entityGizmoSession_.entityUUID != world.GetUUID(entity)) {
			FinalizeEntityGizmoSession(context, world);
		}

		// 現在のマニピュレーター操作を取得
		bool use2DTarget = Prefers2DGizmo(context, world, entity);
		const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, use2DTarget);
		if (!camera) {
			FinalizeEntityGizmoSession(context, world);
			return;
		}

		// ギズモの描画に必要な情報をまとめた構造体を作成
		GizmoViewContext gizmoContext{};
		gizmoContext.rect = rect;
		gizmoContext.viewMatrix = camera->matrices.viewMatrix;
		gizmoContext.projectionMatrix = camera->matrices.projectionMatrix;
		gizmoContext.parentWorldMatrix = GetEntityParentWorldMatrix(world, entity);
		gizmoContext.mode = context.editorState->sceneViewManipulatorMode;
		gizmoContext.orthographic = camera == &context.sceneRenderView->orthographic;
		gizmoContext.allowAxisFlip = !use2DTarget;

		// スナップ有効時はmodeと次元に応じたグリッド単位をImGuizmoへ渡す
		const GridSnapAxis* snapAxis = nullptr;
		if (context.editorState->enableSnapEditEntity) {

			snapAxis = SelectSnapAxis(context.editorState->snapSettings, gizmoContext.mode, use2DTarget);
			if (snapAxis && snapAxis->size > 0.0f) {

				gizmoContext.useSnap = true;
				gizmoContext.snapValues[0] = snapAxis->size;
				gizmoContext.snapValues[1] = snapAxis->size;
				gizmoContext.snapValues[2] = snapAxis->size;
			}
		}

		TransformComponent previewTransform = world.GetComponent<TransformComponent>(entity);

		// ギズモを描画し、操作結果を取得する
		const GizmoEditResult result = use2DTarget ? MyGUI::Manipulate2D("##SceneEntityGizmo2D", gizmoContext, previewTransform) :
			MyGUI::Manipulate3D("##SceneEntityGizmo3D", gizmoContext, previewTransform);

		// 使用しているか
		context.editorState->useSceneGizmo = result.IsUse();

		if (result.isUsing && !entityGizmoSession_.active) {

			entityGizmoSession_.active = true;
			entityGizmoSession_.entityUUID = world.GetUUID(entity);
			entityGizmoSession_.beforeTransform = world.GetComponent<TransformComponent>(entity);
		}

		// 値が変更された場合はプレビュー設定をエンティティに適用する
		if (result.valueChanged) {

			// 絶対スナップ指定なら、操作対象の成分を最寄りのグリッドへ丸めてから適用する
			if (snapAxis && snapAxis->absolute && snapAxis->size > 0.0f) {
				ApplyAbsoluteSnap(previewTransform, gizmoContext.mode, snapAxis->size);
			}
			TransformEditUtility::ApplyImmediate(world, entity, previewTransform);
		}
		// 使用を終了した場合はセッションを終了する
		if (entityGizmoSession_.active && !result.isUsing) {

			FinalizeEntityGizmoSession(context, world);
		}
	}
}

void Engine::ViewportPanel::FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!entityGizmoSession_.active) {
		return;
	}

	if (context.host) {
		const Entity entity = world.FindByUUID(entityGizmoSession_.entityUUID);
		if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {

			const TransformComponent afterTransform = world.GetComponent<TransformComponent>(entity);
			// トランスフォームの値が変更されている場合はコマンドを実行して変更を記録する
			if (!SetTransformCommand::NearlyEqualTransform(entityGizmoSession_.beforeTransform, afterTransform)) {

				context.host->ExecuteEditorCommand(std::make_unique<SetTransformCommand>(entity,
					entityGizmoSession_.beforeTransform, afterTransform));
			}
		}
	}
	entityGizmoSession_ = {};
}

void Engine::ViewportPanel::DrawMultiEntityGizmo(const EditorPanelContext& context, ECSWorld& world,
	const GizmoViewportRect& rect) {

	// 生存かつTransformを持つ対象だけ集め、中心を求める
	std::vector<Entity> targets{};
	Vector3 centerSum = Vector3::AnyInit(0.0f);
	for (const Entity& entity : context.editorState->GetSelectedEntities()) {

		if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {
			targets.push_back(entity);
			centerSum += world.GetComponent<TransformComponent>(entity).worldMatrix.GetTranslationValue();
		}
	}
	if (targets.size() < 2) {
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}
	const Vector3 center = centerSum / static_cast<float>(targets.size());

	// 次元はアクティブなエンティティに合わせる、選択は同次元なので代表でよい
	const bool use2DTarget = Prefers2DGizmo(context, world, context.editorState->selectedEntity);
	const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, use2DTarget);
	if (!camera) {
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}

	// ドラッグ中はピボットを持続させ、idleは中心へ単位姿勢で置く
	TransformComponent pivot{};
	if (multiGizmoSession_.active) {
		pivot = multiGizmoSession_.pivot;
	} else {
		pivot.localPos = center;
		pivot.localRotation = Quaternion::Identity();
		pivot.localScale = Vector3::AnyInit(1.0f);
	}
	const TransformComponent prevPivot = pivot;

	GizmoViewContext gizmoContext{};
	gizmoContext.rect = rect;
	gizmoContext.viewMatrix = camera->matrices.viewMatrix;
	gizmoContext.projectionMatrix = camera->matrices.projectionMatrix;
	gizmoContext.parentWorldMatrix = Matrix4x4::Identity();
	gizmoContext.mode = context.editorState->sceneViewManipulatorMode;
	gizmoContext.orthographic = camera == &context.sceneRenderView->orthographic;
	gizmoContext.allowAxisFlip = !use2DTarget;

	const GizmoEditResult result = use2DTarget ?
		MyGUI::Manipulate2D("##SceneMultiGizmo2D", gizmoContext, pivot) :
		MyGUI::Manipulate3D("##SceneMultiGizmo3D", gizmoContext, pivot);

	context.editorState->useSceneGizmo = result.IsUse();

	// ドラッグ開始時にundo用の操作前姿勢を控える
	if (result.isUsing && !multiGizmoSession_.active) {

		multiGizmoSession_.active = true;
		multiGizmoSession_.beforeTransforms.clear();
		for (const Entity& entity : targets) {
			multiGizmoSession_.beforeTransforms.emplace_back(
				world.GetUUID(entity), world.GetComponent<TransformComponent>(entity));
		}
	}

	// ピボットのフレーム差分を各エンティティへ個別原点で適用する
	if (multiGizmoSession_.active && result.valueChanged) {

		const Vector3 deltaPos = pivot.localPos - prevPivot.localPos;
		const Quaternion deltaRot = pivot.localRotation * Quaternion::Inverse(prevPivot.localRotation);
		const Vector3 deltaScale(
			prevPivot.localScale.x != 0.0f ? pivot.localScale.x / prevPivot.localScale.x : 1.0f,
			prevPivot.localScale.y != 0.0f ? pivot.localScale.y / prevPivot.localScale.y : 1.0f,
			prevPivot.localScale.z != 0.0f ? pivot.localScale.z / prevPivot.localScale.z : 1.0f);

		// 中心ピボットなら位置を中心周りにorbitさせ、個別原点なら位置はそのままにする
		const bool pivotAtCenter = context.editorState->gizmoPivotAtCenter;
		const Vector3 pivotCenter = prevPivot.localPos;
		for (const Entity& entity : targets) {

			TransformComponent transform = world.GetComponent<TransformComponent>(entity);
			// 移動は共通デルタ
			transform.localPos = transform.localPos + deltaPos;
			if (pivotAtCenter) {

				// 回転と拡縮で位置を選択中心周りに動かす、modeは排他なので片方は単位
				const Vector3 offset = transform.localPos - pivotCenter;
				transform.localPos = pivotCenter + RotateVectorByQuaternion(deltaRot, offset) * deltaScale;
			}
			// 回転と拡縮は各自のトランスフォームへ相対適用する
			transform.localRotation = Quaternion::Normalize(deltaRot * transform.localRotation);
			transform.localScale = transform.localScale * deltaScale;
			TransformEditUtility::ApplyImmediate(world, entity, transform);
		}
	}

	if (multiGizmoSession_.active && !result.isUsing) {
		FinalizeMultiEntityGizmoSession(context, world);
	} else if (multiGizmoSession_.active) {
		multiGizmoSession_.pivot = pivot;
	}
}

void Engine::ViewportPanel::FinalizeMultiEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!multiGizmoSession_.active) {
		return;
	}
	if (context.host && context.editorState) {

		// SetTransformCommandは非アクティブ対象を単一選択へ戻すため、複数選択を退避して後で復元する
		const std::vector<Entity> savedSelection = context.editorState->GetSelectedEntities();

		// 操作前後で変化したエンティティだけまとめてコマンド化する
		for (const auto& [uuid, beforeTransform] : multiGizmoSession_.beforeTransforms) {

			const Entity entity = world.FindByUUID(uuid);
			if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
				continue;
			}
			const TransformComponent afterTransform = world.GetComponent<TransformComponent>(entity);
			if (!SetTransformCommand::NearlyEqualTransform(beforeTransform, afterTransform)) {

				context.host->ExecuteEditorCommand(
					std::make_unique<SetTransformCommand>(entity, beforeTransform, afterTransform));
			}
		}
		// 退避していた複数選択を復元する
		context.editorState->SetSelectedEntities(savedSelection);
	}
	multiGizmoSession_ = {};
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
	textureUploadService_->RequestTextureFile(icons_.manualCamera2DKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.manualCamera2DKey));
	textureUploadService_->RequestTextureFile(icons_.manualCamera3DKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.manualCamera3DKey));
	textureUploadService_->RequestTextureFile(icons_.drawGridKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.drawGridKey));
	textureUploadService_->RequestTextureFile(icons_.gizmoCenterPivotKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.gizmoCenterPivotKey));
	textureUploadService_->RequestTextureFile(icons_.eachEntityOriginKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.eachEntityOriginKey));
	textureUploadService_->RequestTextureFile(icons_.snapEditEntityKey,
		EditorTextureHelper::MakeEditorTexturePath("Tool", icons_.snapEditEntityKey));
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

	// マニュアルカメラの次元を切り替えるボタン
	Dimension& operationMode = context.editorState->manualCameraDimension;

	bool is3D = (operationMode == Dimension::Type3D);
	// アイコンは次元に応じて変える
	ImTextureID modeIcon = is3D ? GetTextureID(icons_.manualCamera3DKey) :
		GetTextureID(icons_.manualCamera2DKey);
	if (DrawIconButton("##SceneDimensionMode", modeIcon, true, buttonSize_)) {

		operationMode = is3D ? Dimension::Type2D : Dimension::Type3D;
	}
	if (ImGui::IsItemHovered()) {

		std::string tooltip = std::string("マニュアルカメラの2D/3D切り替え\n現在の状態: ") +
			(operationMode == Dimension::Type2D ? "2D" : "3D");
		ImGui::SetTooltip("%s", tooltip.c_str());
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
		if (DrawIconButton("##ManipulatorNone", GetTextureID(icons_.noneKey),
			mode == SceneViewManipulatorMode::None, buttonSize_)) {

			mode = SceneViewManipulatorMode::None;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("マニュピレーター表示なし");
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
				(kind == EditorSelectionKind::Entity ? "エンティティ単位" : "サブメッシュ単位");
			ImGui::SetTooltip("%s", tooltip.c_str());
		}
	}
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	// SRT編集
	{
		if (DrawIconButton("##ManipulatorTranslate", GetTextureID(icons_.translateKey),
			mode == SceneViewManipulatorMode::Translate, buttonSize_)) {

			mode = SceneViewManipulatorMode::Translate;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("座標編集");
		}
		if (DrawIconButton("##ManipulatorRotate", GetTextureID(icons_.rotateKey),
			mode == SceneViewManipulatorMode::Rotate, buttonSize_)) {

			mode = SceneViewManipulatorMode::Rotate;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("回転編集");
		}
		if (DrawIconButton("##ManipulatorScale", GetTextureID(icons_.scaleKey),
			mode == SceneViewManipulatorMode::Scale, buttonSize_)) {

			mode = SceneViewManipulatorMode::Scale;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("拡縮編集");
		}
	}
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	// オブジェクトのスナップ操作の有効/無効
	{
		if (DrawIconButton("##EnableSnapEntity", GetTextureID(icons_.snapEditEntityKey),
			context.editorState->enableSnapEditEntity, buttonSize_)) {

			context.editorState->enableSnapEditEntity = !context.editorState->enableSnapEditEntity;
		}
		if (ImGui::IsItemHovered()) {

			std::string tooltip = std::string("エンティティスナップ操作の有効/無効切り替え\n左クリックで切替 右クリックで設定\n現在の状態: ") +
				(context.editorState->enableSnapEditEntity ? "有効" : "無効");
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
	DrawCameraChoiceCombo("2D Camera", selection.orthographicCameraUUID, orthoChoices, "<Auto 2D>");
	DrawCameraChoiceCombo("3D Camera", selection.perspectiveCameraUUID, perspChoices, "<Auto 3D>");
	ImGui::PopItemWidth();

	ImGui::Separator();

	if (ImGui::Button("Clear")) {

		selection.ClearAssignedCameras();
	}
	ImGui::SameLine();
	if (ImGui::Button("Use Debug Camera")) {

		selection.mode = SceneViewCameraMode::DebugManual;
		selection.ClearAssignedCameras();
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

