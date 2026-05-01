#include "CameraManagerTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/CameraComponent.h>
#include <Engine/Core/ECS/Component/Builtin/CameraControllerComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Editor/Command/Methods/AddComponentCommand.h>
#include <Engine/Editor/Command/Methods/CreateEntityCommand.h>
#include <Engine/Editor/Command/Methods/SetSerializedComponentCommand.h>
#include <Engine/Editor/EditorState.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>

// imgui
#include <imgui.h>

// c++
#include <memory>
#include <string>

//============================================================================
//	CameraManagerTool classMethods
//============================================================================

namespace {

	// Entityの表示名を取得する
	std::string GetEntityName(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity)) {
			return "None";
		}
		if (world.HasComponent<Engine::NameComponent>(entity)) {
			return world.GetComponent<Engine::NameComponent>(entity).name;
		}
		return Engine::ToString(world.GetUUID(entity));
	}

	// コンポーネントを差し替えるコマンドを実行する
	template <typename T>
	void SetComponent(Engine::IEditorPanelHost& host,
		const Engine::Entity& entity, const char* typeName, const T& beforeComponent, const T& afterComponent) {

		nlohmann::json beforeData = beforeComponent;
		nlohmann::json afterData = afterComponent;
		host.ExecuteEditorCommand(std::make_unique<Engine::SetSerializedComponentCommand>(
			entity, typeName, beforeData, afterData));
	}

	// Entityを作成して、作成後に選択されたEntityを返す
	Engine::Entity CreateCameraEntity(const Engine::EditorToolContext& context, const std::string& name) {

		if (!context.panelContext || !context.panelContext->host ||
			!context.panelContext->editorState || !context.CanEditScene()) {

			return Engine::Entity::Null();
		}

		if (!context.panelContext->host->ExecuteEditorCommand(std::make_unique<Engine::CreateEntityCommand>(name))) {
			return Engine::Entity::Null();
		}

		Engine::ECSWorld* world = context.GetWorld();
		const Engine::Entity created = context.panelContext->editorState->selectedEntity;
		if (!world || !world->IsAlive(created)) {
			return Engine::Entity::Null();
		}
		return created;
	}

	// CameraControllerにターゲットを設定する
	void SetControllerTarget(Engine::CameraControllerComponent& controller, Engine::UUID target) {

		controller.follow.target = target;
		controller.lookAt.target = target;
	}
}

void Engine::CameraManagerTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::CameraManagerTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::CameraManagerTool::DrawWindow(const EditorToolContext& context) {

	if (!ImGui::Begin("CameraManager", &openWindow_)) {
		ImGui::End();
		return;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !context.panelContext || !context.panelContext->editorState) {

		ImGui::TextDisabled("World is not available.");
		ImGui::End();
		return;
	}

	const Entity selected = context.panelContext->editorState->selectedEntity;
	ImGui::Text("Selected Target : %s", GetEntityName(*world, selected).c_str());
	ImGui::Separator();

	if (!context.CanEditScene()) {
		ImGui::BeginDisabled();
	}

	// よく使うカメラ構成を作成する
	if (ImGui::Button("Create 2D Follow Camera", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		Create2DFollowCamera(context);
	}
	if (ImGui::Button("Create 3D Follow Camera", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		Create3DFollowCamera(context);
	}
	if (ImGui::Button("Create 3D LookAt Camera", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		Create3DLookAtCamera(context);
	}

	ImGui::Spacing();
	ImGui::Separator();

	// 既存カメラへ制御コンポーネントを追加する
	if (ImGui::Button("Add CameraController To Selected", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		AddControllerToSelected(context);
	}
	if (ImGui::Button("Assign Selected Camera To SceneView", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		AssignSelectedCameraToSceneView(context);
	}

	if (!context.CanEditScene()) {
		ImGui::EndDisabled();
	}

	ImGui::End();
}

Engine::UUID Engine::CameraManagerTool::GetSelectedTargetUUID(const EditorToolContext& context) const {

	if (!context.panelContext || !context.panelContext->editorState || !context.GetWorld()) {
		return UUID{};
	}

	ECSWorld& world = *context.GetWorld();
	const Entity selected = context.panelContext->editorState->selectedEntity;
	if (!world.IsAlive(selected)) {
		return UUID{};
	}
	return world.GetUUID(selected);
}

void Engine::CameraManagerTool::Create2DFollowCamera(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !context.panelContext || !context.panelContext->host) {
		return;
	}

	const UUID target = GetSelectedTargetUUID(context);
	const Entity camera = CreateCameraEntity(context, "FollowCamera2D");
	if (!world->IsAlive(camera)) {
		return;
	}

	context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(camera, "OrthographicCamera"));
	context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(camera, "CameraController"));

	auto transform = world->GetComponent<TransformComponent>(camera);
	const TransformComponent beforeTransform = transform;
	transform.localPos = Vector3(0.0f, 0.0f, -10.0f);
	SetComponent(*context.panelContext->host, camera, "Transform", beforeTransform, transform);

	auto cameraComponent = world->GetComponent<OrthographicCameraComponent>(camera);
	const OrthographicCameraComponent beforeCamera = cameraComponent;
	cameraComponent.common.isMain = true;
	cameraComponent.nearClip = 0.0f;
	cameraComponent.farClip = 1000.0f;
	SetComponent(*context.panelContext->host, camera, "OrthographicCamera", beforeCamera, cameraComponent);

	auto controller = world->GetComponent<CameraControllerComponent>(camera);
	const CameraControllerComponent beforeController = controller;
	controller.mode = CameraControlMode::Follow;
	controller.follow.offset = Vector3(0.0f, 0.0f, -10.0f);
	controller.follow.axisMask = Vector3(1.0f, 1.0f, 0.0f);
	controller.lookAt.enabled = false;
	SetControllerTarget(controller, target);
	SetComponent(*context.panelContext->host, camera, "CameraController", beforeController, controller);

	if (context.panelContext->editorState) {
		context.panelContext->editorState->sceneViewCamera.orthographicCameraUUID = world->GetUUID(camera);
		context.panelContext->editorState->sceneViewCamera.mode = SceneViewCameraMode::SelectedEntityCamera;
	}
}

void Engine::CameraManagerTool::Create3DFollowCamera(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !context.panelContext || !context.panelContext->host) {
		return;
	}

	const UUID target = GetSelectedTargetUUID(context);
	const Entity camera = CreateCameraEntity(context, "FollowCamera3D");
	if (!world->IsAlive(camera)) {
		return;
	}

	context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(camera, "PerspectiveCamera"));
	context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(camera, "CameraController"));

	auto transform = world->GetComponent<TransformComponent>(camera);
	const TransformComponent beforeTransform = transform;
	transform.localPos = Vector3(0.0f, 3.0f, -8.0f);
	SetComponent(*context.panelContext->host, camera, "Transform", beforeTransform, transform);

	auto cameraComponent = world->GetComponent<PerspectiveCameraComponent>(camera);
	const PerspectiveCameraComponent beforeCamera = cameraComponent;
	cameraComponent.common.isMain = true;
	cameraComponent.fovY = 60.0f;
	cameraComponent.nearClip = 0.01f;
	cameraComponent.farClip = 4000.0f;
	SetComponent(*context.panelContext->host, camera, "PerspectiveCamera", beforeCamera, cameraComponent);

	auto controller = world->GetComponent<CameraControllerComponent>(camera);
	const CameraControllerComponent beforeController = controller;
	controller.mode = CameraControlMode::FollowLookAt;
	controller.follow.offset = Vector3(0.0f, 3.0f, -8.0f);
	controller.follow.axisMask = Vector3::AnyInit(1.0f);
	controller.lookAt.enabled = true;
	controller.lookAt.lockRoll = true;
	SetControllerTarget(controller, target);
	SetComponent(*context.panelContext->host, camera, "CameraController", beforeController, controller);

	if (context.panelContext->editorState) {
		context.panelContext->editorState->sceneViewCamera.perspectiveCameraUUID = world->GetUUID(camera);
		context.panelContext->editorState->sceneViewCamera.mode = SceneViewCameraMode::SelectedEntityCamera;
	}
}

void Engine::CameraManagerTool::Create3DLookAtCamera(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !context.panelContext || !context.panelContext->host) {
		return;
	}

	const UUID target = GetSelectedTargetUUID(context);
	const Entity camera = CreateCameraEntity(context, "LookAtCamera3D");
	if (!world->IsAlive(camera)) {
		return;
	}

	context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(camera, "PerspectiveCamera"));
	context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(camera, "CameraController"));

	auto transform = world->GetComponent<TransformComponent>(camera);
	const TransformComponent beforeTransform = transform;
	transform.localPos = Vector3(0.0f, 4.0f, -10.0f);
	SetComponent(*context.panelContext->host, camera, "Transform", beforeTransform, transform);

	auto cameraComponent = world->GetComponent<PerspectiveCameraComponent>(camera);
	const PerspectiveCameraComponent beforeCamera = cameraComponent;
	cameraComponent.common.isMain = true;
	SetComponent(*context.panelContext->host, camera, "PerspectiveCamera", beforeCamera, cameraComponent);

	auto controller = world->GetComponent<CameraControllerComponent>(camera);
	const CameraControllerComponent beforeController = controller;
	controller.mode = CameraControlMode::LookAt;
	controller.follow.enabled = false;
	controller.lookAt.enabled = true;
	controller.lookAt.lockRoll = true;
	SetControllerTarget(controller, target);
	SetComponent(*context.panelContext->host, camera, "CameraController", beforeController, controller);

	if (context.panelContext->editorState) {
		context.panelContext->editorState->sceneViewCamera.perspectiveCameraUUID = world->GetUUID(camera);
		context.panelContext->editorState->sceneViewCamera.mode = SceneViewCameraMode::SelectedEntityCamera;
	}
}

void Engine::CameraManagerTool::AddControllerToSelected(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->host || !context.panelContext->editorState || !context.GetWorld()) {
		return;
	}

	ECSWorld& world = *context.GetWorld();
	const Entity selected = context.panelContext->editorState->selectedEntity;
	if (!world.IsAlive(selected) || world.HasComponent<CameraControllerComponent>(selected)) {
		return;
	}
	if (!world.HasComponent<OrthographicCameraComponent>(selected) &&
		!world.HasComponent<PerspectiveCameraComponent>(selected)) {

		return;
	}

	context.panelContext->host->ExecuteEditorCommand(std::make_unique<AddComponentCommand>(selected, "CameraController"));
}

void Engine::CameraManagerTool::AssignSelectedCameraToSceneView(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->editorState || !context.GetWorld()) {
		return;
	}

	ECSWorld& world = *context.GetWorld();
	const Entity selected = context.panelContext->editorState->selectedEntity;
	if (!world.IsAlive(selected)) {
		return;
	}

	const UUID cameraUUID = world.GetUUID(selected);
	if (world.HasComponent<OrthographicCameraComponent>(selected)) {
		context.panelContext->editorState->sceneViewCamera.orthographicCameraUUID = cameraUUID;
	}
	if (world.HasComponent<PerspectiveCameraComponent>(selected)) {
		context.panelContext->editorState->sceneViewCamera.perspectiveCameraUUID = cameraUUID;
	}
	context.panelContext->editorState->sceneViewCamera.mode = SceneViewCameraMode::SelectedEntityCamera;
}
