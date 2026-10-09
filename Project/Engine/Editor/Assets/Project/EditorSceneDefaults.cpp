#include "EditorSceneDefaults.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

namespace {

	// 新規Scene内の識別子と共通Componentを揃える
	nlohmann::json MakeEntity(const char* name, int32_t order) {

		const Engine::UUID id = Engine::UUID::New();
		Engine::NameComponent entityName{};
		entityName.name = name;
		Engine::SceneObjectComponent sceneObject{};
		sceneObject.localFileID = id;
		Engine::TransformComponent transform{};
		transform.dimension = Engine::Dimension::Type3D;
		Engine::HierarchyComponent hierarchy{};
		hierarchy.siblingOrder = order;
		return { { "LocalFileID", Engine::ToString(id) }, { "Components", {
			{ "Name", entityName }, { "SceneObject", sceneObject }, { "Transform", transform }, { "Hierarchy", hierarchy } } } };
	}
}

Engine::PerspectiveCameraComponent Engine::EditorSceneDefaults::MakeCamera() {

	PerspectiveCameraComponent camera{};
	// 新規カメラへEngineの標準Passを設定する
	camera.common.renderPasses = BuiltinAssets::RenderPasses::Default;
	return camera;
}

nlohmann::json Engine::EditorSceneDefaults::MakeScene(const std::string& name) {

	auto camera = MakeEntity("Camera3D", 0);
	camera["Components"]["PerspectiveCamera"] = MakeCamera();
	TransformComponent cameraTransform{};
	cameraTransform.dimension = Dimension::Type3D;
	cameraTransform.localPos = Vector3(0.0f, 2.0f, -10.0f);
	camera["Components"]["Transform"] = cameraTransform;
	auto light = MakeEntity("DirectionalLight", 1);
	light["Components"]["DirectionalLight"] = DirectionalLightComponent{};
	// 初回保存で既存の外部Actor形式へ移行する
	return { { "SchemaVersion", 3 }, { "Header", { { "name", name }, { "subScenes", nlohmann::json::array() } } },
		{ "Entities", nlohmann::json::array({ camera, light }) }, { "PrefabInstances", nlohmann::json::array() } };
}
