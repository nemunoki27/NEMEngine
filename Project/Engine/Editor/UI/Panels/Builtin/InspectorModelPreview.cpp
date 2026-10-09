#include "InspectorModelPreview.h"
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraController.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Editor/Assets/Preview/ModelPreviewUtility.h>

// c++
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>
#include <span>


Engine::InspectorModelPreview::InspectorModelPreview() {

	modelPreviewCameraController_ =
		std::make_unique<SceneViewCameraController>(false);
	modelPreviewCameraController_->MakeDefaultState();
	modelPreviewCameraController_->SetSavePath(RuntimePaths::GetUserSettingsPath(
		ConfigPaths::kInspectorModelPreviewCamera).string());

}

void Engine::InspectorModelPreview::DrawMeshAssetInspector(const EditorPanelContext& context, const AssetMeta& meta) {

	if (!context.editorContext || !context.editorContext->assetDatabase || !context.editorState) {

		ImGui::TextDisabled("Mesh preview is not available.");
		return;
	}

	const uint64_t selectionRevision = context.editorState->assetSelectionRevision;
	if (modelPreviewAsset_ != meta.guid || modelPreviewSelectionRevision_ != selectionRevision ||
		modelPreviewDatabaseLifetime_.lock() != context.editorContext->assetDatabase->GetCacheLifetime().lock()) {

		modelPreviewSelectionRevision_ = selectionRevision;
		RebuildModelAssetPreviewWorld(context, meta);
	}
	// 境界も非同期Mesh読込の結果から取得する
	const AssetID assets[] = { meta.guid };
	if (context.graphicsCore && context.renderPipeline) {
		context.renderPipeline->PreparePreviewMeshes(*context.graphicsCore, *context.editorContext->assetDatabase, assets);
		if (const auto* mesh = context.renderPipeline->FindPreviewMesh(meta.guid);
			mesh && (!modelPreviewBounds_.valid || modelPreviewMeshGeneration_ != mesh->reloadGeneration)) {
			modelPreviewMeshGeneration_ = mesh->reloadGeneration;
			// 詳細プレビューも公開済みの色と画像を使う
			const auto materials = ModelPreviewUtility::BuildMaterials(mesh->subMeshes);
			SetMeshSubMeshes(*modelPreviewWorld_, modelPreviewEntity_, materials);
			modelPreviewRendered_ = false;
			modelPreviewBounds_.valid = true;
			modelPreviewBounds_.center = mesh->boundsCenter;
			modelPreviewBounds_.radius = (std::max)(mesh->boundsRadius, 0.1f);
			modelPreviewBounds_.min = mesh->boundsCenter - Vector3::AnyInit(modelPreviewBounds_.radius);
			modelPreviewBounds_.max = mesh->boundsCenter + Vector3::AnyInit(modelPreviewBounds_.radius);
			ResetModelAssetPreviewCamera();
		}
	}

	ImGui::Text("Mesh Preview");
	ImGui::Separator();

	const float availableWidth = (std::max)(ImGui::GetContentRegionAvail().x, 64.0f);
	const float displayWidth = (std::min)(availableWidth, static_cast<float>(kModelPreviewSize_.x));
	const float displayHeight = displayWidth * static_cast<float>(kModelPreviewSize_.y) /
		static_cast<float>((std::max)(kModelPreviewSize_.x, 1));
	const ImVec2 displaySize(displayWidth, displayHeight);

	if (!context.graphicsCore || !context.renderPipeline || !modelPreviewWorld_ ||
		!modelPreviewWorld_->IsAlive(modelPreviewEntity_)) {

		ImGui::Dummy(displaySize);
		ImGui::TextDisabled("Mesh preview render target is not available.");
		return;
	}

	modelPreviewImagePos_ = ImGui::GetCursorScreenPos();
	Input::GetInstance()->SetViewRect(InputViewArea::InspectorModelPreview,
		Vector2(modelPreviewImagePos_.x, modelPreviewImagePos_.y),
		Vector2(displaySize.x, displaySize.y),
		EngineContext::GetWindowSetting().gameSize.GetFloat(),
		InputViewCoordinateSpace::Screen);

	ToolContext toolContext{};
	toolContext.world = context.editorContext->activeWorld;
	toolContext.assetDatabase = context.editorContext->assetDatabase;
	toolContext.sceneInstances = context.editorContext->sceneInstances;
	toolContext.activeSceneHeader = context.editorContext->activeSceneHeader;
	toolContext.activeSceneAsset = context.editorContext->activeSceneAsset;
	toolContext.activeSceneInstanceID = context.editorContext->activeSceneInstanceID;
	toolContext.activeScenePath = context.editorContext->activeScenePath;
	toolContext.isPlaying = context.IsPlaying();
	toolContext.canEditScene = context.CanEditScene();

	EditorToolContext editorToolContext{};
	editorToolContext.panelContext = &context;
	editorToolContext.toolContext = toolContext;

	resources_.BeginEditorToolFrame(editorToolContext);
	EditorToolRenderTexture* preview = resources_.CreateRenderTexture("InspectorModelAssetPreview",
		kModelPreviewSize_, kModelPreviewColor_, kModelPreviewColorTargetCount_);
	if (preview) {

		RenderModelAssetPreview(editorToolContext, *preview);
		// 画像上のホイールはPreviewだけが消費する
		ImGui::GetWindowDrawList()->AddImage(preview->GetImTextureID(), modelPreviewImagePos_,
			ImVec2(modelPreviewImagePos_.x + displaySize.x, modelPreviewImagePos_.y + displaySize.y));
		ImGui::InvisibleButton("##MeshPreviewInput", displaySize);
		ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
	} else {

		ImGui::Dummy(displaySize);
		ImGui::TextDisabled("Mesh preview render target is not available.");
	}
	resources_.EndEditorToolFrame();
}

void Engine::InspectorModelPreview::RebuildModelAssetPreviewWorld(const EditorPanelContext& context, const AssetMeta& meta) {

	modelPreviewAsset_ = meta.guid;
	modelPreviewWorld_ = std::make_unique<ECSWorld>();
	modelPreviewEntity_ = Entity::Null();
	modelPreviewLightEntity_ = Entity::Null();
	modelPreviewBounds_ = {};
	modelPreviewMeshGeneration_ = 0;
	modelPreviewRendered_ = false;
	modelPreviewDatabaseLifetime_ = context.editorContext->assetDatabase->GetCacheLifetime();

	AssetDatabase* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (!database || !meta.guid) {

		ResetModelAssetPreviewCamera();
		return;
	}

	Entity lightEntity = modelPreviewWorld_->CreateEntity(UUID::New());
	auto& lightTransform = modelPreviewWorld_->AddComponent<TransformComponent>(lightEntity);
	lightTransform.worldMatrix = Matrix4x4::Identity();
	lightTransform.isDirty = false;
	auto& light = modelPreviewWorld_->AddComponent<DirectionalLightComponent>(lightEntity);
	light.direction = Vector3(0.35f, -0.65f, 0.65f).Normalize();
	light.intensity = 1.5f;
	modelPreviewLightEntity_ = lightEntity;

	Entity entity = modelPreviewWorld_->CreateEntity(UUID::New());
	auto& transform = modelPreviewWorld_->AddComponent<TransformComponent>(entity);
	transform.worldMatrix = Matrix4x4::Identity();
	transform.isDirty = false;

	auto& renderer = modelPreviewWorld_->AddComponent<MeshRendererComponent>(entity);
	renderer.mesh = meta.guid;
	renderer.material = {};
	renderer.queue = RenderPhase::Opaque;
	renderer.visible = true;
	renderer.enableZPrepass = true;

	modelPreviewEntity_ = entity;
	ResetModelAssetPreviewCamera();
}

void Engine::InspectorModelPreview::RenderModelAssetPreview(const EditorToolContext& toolContext,
	EditorToolRenderTexture& preview) {

	if (!toolContext.panelContext || !toolContext.panelContext->renderPipeline || !modelPreviewWorld_ ||
		!modelPreviewWorld_->IsAlive(modelPreviewEntity_)) {

		resources_.RenderToTexture(preview, [](EditorToolRenderContext&) {}, preview.clearColor);
		return;
	}

	modelPreviewCameraController_->Update(Dimension::Type3D, InputViewArea::InspectorModelPreview);
	const auto& camera = modelPreviewCameraController_->GetCameraState();
	const uint64_t textureRevision = toolContext.panelContext->graphicsCore->GetTextureUploadService().GetContentRevision();
	// 連続したTexture読込ごとの再描画をまとめる
	modelPreviewTextureQuietFrames_ = modelPreviewObservedTextureRevision_ == textureRevision ?
		(std::min)(modelPreviewTextureQuietFrames_ + 1, 8u) : 0;
	modelPreviewObservedTextureRevision_ = textureRevision;
	// CameraとAssetが変わったときだけ描き直す
	if (modelPreviewRendered_ && modelPreviewCameraPos_ == camera.transform3D.pos &&
		modelPreviewCameraRotation_ == camera.transform3D.rotation && (modelPreviewTextureRevision_ == textureRevision || modelPreviewTextureQuietFrames_ < 8)) {
		return;
	}
	resources_.RenderToTexture(preview, [&](EditorToolRenderContext& renderContext) {

		EntityPreviewRenderRequest request{};
		request.world = modelPreviewWorld_.get();
		request.systemContext = toolContext.toolContext.systemContext;
		request.assetDatabase = toolContext.toolContext.assetDatabase;
		request.sceneHeader = toolContext.toolContext.activeSceneHeader;
		request.sceneInstanceID = {};
		request.rootEntity = modelPreviewEntity_;
		request.surface = preview.GetRenderTarget();
		request.camera = modelPreviewCameraController_->GetCameraState();
		request.clearColor = preview.clearColor;
		request.drawGrid3D = true;

		modelPreviewRendered_ = toolContext.panelContext->renderPipeline->RenderEntityPreview(*renderContext.graphicsCore, request);
		}, preview.clearColor);
	modelPreviewCameraPos_ = camera.transform3D.pos;
	modelPreviewCameraRotation_ = camera.transform3D.rotation;
	modelPreviewTextureRevision_ = textureRevision;
}

void Engine::InspectorModelPreview::ResetModelAssetPreviewCamera() {

	if (!modelPreviewCameraController_) {
		return;
	}

	const Vector3 center = modelPreviewBounds_.valid ? modelPreviewBounds_.center : Vector3::AnyInit(0.0f);
	const float radius = (std::max)(modelPreviewBounds_.valid ? modelPreviewBounds_.radius : 1.0f, 0.1f);
	const float fovY = 35.0f;
	const float pitchDegrees = 8.0f;
	const float yawDegrees = 180.0f;
	const float aspectRatio = static_cast<float>(kModelPreviewSize_.x) /
		static_cast<float>((std::max)(kModelPreviewSize_.y, 1));
	const float distance = modelPreviewBounds_.valid ?
		ModelPreviewUtility::CalculateCameraDistance(modelPreviewBounds_.min, modelPreviewBounds_.max, center,
			pitchDegrees, yawDegrees, fovY, aspectRatio, 2.0f) :
		radius * 2.0f;
	const Matrix4x4 cameraRotation = Matrix4x4::MakeRotateMatrix(Vector3(pitchDegrees, yawDegrees, 0.0f));
	const Vector3 cameraForward(cameraRotation.m[2][0], cameraRotation.m[2][1], cameraRotation.m[2][2]);

	ManualRenderCameraState& camera = modelPreviewCameraController_->GetCameraState();
	camera = {};
	camera.enableOrthographic = false;
	camera.enablePerspective = true;
	camera.perspectiveFovY = fovY;
	camera.perspectiveNearClip = 0.01f;
	camera.perspectiveFarClip = (std::max)(4000.0f, distance + radius * 4.0f);
	camera.perspectiveCullingMask = -1;
	camera.transform3D.pos = center - cameraForward * distance;
	camera.transform3D.rotation = Vector3(pitchDegrees, yawDegrees, 0.0f);
}
