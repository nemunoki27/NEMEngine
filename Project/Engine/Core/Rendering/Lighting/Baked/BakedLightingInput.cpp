#include "BakedLightingInput.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/BuiltinLightExtractors.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightGPUConversion.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Rendering/ReflectionProbeComponent.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <stdexcept>
#include <cmath>
#include <unordered_set>

namespace {

	using namespace Engine;
	// Active Sceneに所属する固定ライトだけ取り出す
	template<typename Component>
	void RestrictLights(ECSWorld& world, Engine::UUID sceneInstance) {

		world.ForEach<Component>([&](Entity entity, Component& light) {
			const auto* owner = world.TryGetComponent<SceneObjectComponent>(entity);
			light.enabled = light.enabled && light.contributeBakedLighting && owner && owner->sceneInstanceID == sceneInstance;
		});
	}
	// 固定ライトとTransformの保存値を安定IDで記録する
	template<typename Component>
	void AppendLightState(ECSWorld& world, AssetID sceneAsset, Engine::UUID sceneInstance, const char* name, nlohmann::json& state) {

		world.ForEach<Component, SceneObjectComponent>([&](Entity entity, const Component& light, const SceneObjectComponent& object) {
			if (!light.contributeBakedLighting || (sceneInstance ? object.sceneInstanceID != sceneInstance : object.sourceAsset != sceneAsset)) return;
			const auto* transform = world.TryGetComponent<TransformComponent>(entity);
			state[std::string(name) + ToString(object.localFileID)] = {{"component", light}, {"active", object.activeInHierarchy},
				{"matrix", transform ? nlohmann::json(transform->worldMatrix.m) : nlohmann::json(Matrix4x4::Identity().m)}};
		});
	}
}

std::string Engine::BakedLightingInputBuilder::ReceiverSettings(const ECSWorld& world, const Entity& entity) {

	nlohmann::json data;
	SerializeMeshRenderer(world.GetComponent<MeshRendererComponent>(entity), GetMeshSubMeshes(world, entity), data);
	data.erase("receiveLightmap");
	data.erase("receiveLightProbes");
	return data.dump();
}

nlohmann::json Engine::BakedLightingInputBuilder::LightingState(ECSWorld& world, AssetID sceneAsset, UUID sceneInstance) {

	nlohmann::json state = nlohmann::json::object();
	AppendLightState<DirectionalLightComponent>(world, sceneAsset, sceneInstance, "directional", state);
	AppendLightState<PointLightComponent>(world, sceneAsset, sceneInstance, "point", state);
	AppendLightState<SpotLightComponent>(world, sceneAsset, sceneInstance, "spot", state);
	AppendLightState<RectLightComponent>(world, sceneAsset, sceneInstance, "rect", state);
	world.ForEach<SkyboxRendererComponent, SceneObjectComponent>([&](Entity, const SkyboxRendererComponent& skybox,
		const SceneObjectComponent& object) {
		if (sceneInstance ? object.sceneInstanceID == sceneInstance : object.sourceAsset == sceneAsset) {
			state["skybox" + ToString(object.localFileID)] = {{"component", skybox}, {"active", object.activeInHierarchy}};
		}
	});
	world.ForEach<ReflectionProbeComponent, SceneObjectComponent>([&](Entity entity, const ReflectionProbeComponent& capture,
		const SceneObjectComponent& object) {
		if (sceneInstance ? object.sceneInstanceID != sceneInstance : object.sourceAsset != sceneAsset) return;
		const auto* transform = world.TryGetComponent<TransformComponent>(entity);
		state["capture" + ToString(object.localFileID)] = {{"component", capture}, {"active", object.activeInHierarchy},
			{"matrix", transform ? nlohmann::json(transform->worldMatrix.m) : nlohmann::json(Matrix4x4::Identity().m)}};
	});
	return state;
}

Engine::BakedLightingInput Engine::BakedLightingInputBuilder::Capture(AssetDatabase& database, ECSWorld& world,
	AssetID sceneAsset, UUID sceneInstance) {

	BakedLightingInput input;
	input.preparation.data.sceneAsset = sceneAsset;
	input.preparation.data.inputRevisions["lightingState"] = LightingState(world, sceneAsset, sceneInstance);
	RenderAssetLibrary library;
	library.Init(&database);
	std::unordered_set<AssetID> assets;
	const auto addAsset = [&](AssetID id) {
		if (!id) return;
		const auto* metadata = database.Find(id);
		if (!metadata) throw std::runtime_error("固定背景が参照するAssetが見つかりません");
		if (metadata->type == AssetType::RenderTexture) throw std::runtime_error("実行時RenderTextureは照明ベイクの入力にできません");
		assets.insert(id);
	};
	// 不変のMaterialとSubMesh編集値を先に取得する
	world.ForEach<MeshRendererComponent, TransformComponent, SceneObjectComponent>([&](Entity entity,
		const MeshRendererComponent& renderer, const TransformComponent& transform, const SceneObjectComponent& object) {
		if (!renderer.contributeBakedLighting || object.sceneInstanceID != sceneInstance || !object.activeInHierarchy || !renderer.visible) return;
		if (world.HasComponent<BillboardComponent>(entity)) throw std::runtime_error("Billboardは固定背景としてベイクできません");
		const auto* metadata = database.Find(renderer.mesh);
		if (!metadata) throw std::runtime_error("固定背景のMesh Assetが見つかりません");
		BakedLightingMeshInput mesh;
		mesh.mesh = renderer.mesh;
		mesh.path = database.ResolveFullPath(renderer.mesh);
		mesh.worldMatrix = transform.worldMatrix;
		mesh.importSettings = ParseMeshImportSettings(metadata->importerSettings);
		mesh.importSettings.generateAutomaticLODs = false;
		mesh.importSettings.manualLODMeshes = {};
		mesh.receiverIndex = static_cast<uint32_t>(input.preparation.data.receivers.size());
		input.preparation.data.receivers.push_back({object.localFileID, transform.worldMatrix, ReceiverSettings(world, entity)});
		const auto subMeshes = GetMeshSubMeshes(world, entity);
		mesh.subMeshes.assign(subMeshes.begin(), subMeshes.end());
		for (const auto& subMesh : mesh.subMeshes) {
			const AssetID materialID = subMesh.material ? subMesh.material : renderer.material ? renderer.material : BuiltinAssets::Materials::DefaultMesh;
			const auto* material = library.LoadMaterial(materialID);
			if (!material) throw std::runtime_error("固定背景のMaterial Assetが見つかりません");
			if (material->shaderGraph) throw std::runtime_error("ShaderGraphの固定背景はベイク用評価に未対応です 標準PBRへ変更してください");
			const auto* draw = FindPass(*material, MaterialPassKind::Draw);
			if (!draw || draw->pipeline != BuiltinAssets::Pipelines::DefaultMesh || draw->shaderOverride) {
				throw std::runtime_error("固定背景のShaderは標準PBRのみベイクできます");
			}
			MaterialParameterSet parameters = material->parameters;
			parameters.MergeFrom(subMesh.materialInstance);
			if (const auto* displacement = parameters.Find(MaterialParameterIDs::DisplacementScale)) {
				if (const auto* scale = std::get_if<float>(&displacement->value); scale && *scale != 0.0f) {
					throw std::runtime_error("Displacementの固定背景はベイク用評価に未対応です");
				}
			}
			BakedLightingMaterialInput materialInput{*material, subMesh.materialInstance, MeshSubMeshRuntime::BuildUVMatrix(subMesh)};
			materialInput.surfaceMode = subMesh.surfaceMode == MaterialSurfaceMode::Auto ? subMesh.sourceSurfaceMode : subMesh.surfaceMode;
			if (material->renderState.overridesRenderer) materialInput.surfaceMode = material->renderState.surfaceMode;
			if (materialInput.surfaceMode == MaterialSurfaceMode::Transparent || renderer.queue == RenderPhase::Transparent) {
				throw std::runtime_error("半透明の固定背景はベイク対象にできません");
			}
			materialInput.alphaCutoff = subMesh.alphaCutoff;
			mesh.materials.push_back(static_cast<uint32_t>(input.preparation.materials.size()));
			input.preparation.materials.push_back(std::move(materialInput));
			addAsset(materialID);
			for (const auto& [name, value] : parameters) {
				if (const auto* asset = std::get_if<AssetID>(&value.value)) addAsset(*asset);
			}
		}
		addAsset(renderer.mesh);
		input.preparation.meshes.push_back(std::move(mesh));
	});
	world.ForEach<ReflectionProbeComponent, TransformComponent, SceneObjectComponent>([&](Entity,
		const ReflectionProbeComponent& capture, const TransformComponent& transform, const SceneObjectComponent& object) {
		if (!capture.enabled || !object.activeInHierarchy || object.sceneInstanceID != sceneInstance) return;
		if (BuildSafeMeshNormalMatrix(transform.worldMatrix).usedFallback) throw std::runtime_error("Reflection CaptureのTransformが退化しています");
		const auto& matrix = transform.worldMatrix;
		const float scale = std::max({Vector3::Length(Vector3(matrix.m[0][0], matrix.m[0][1], matrix.m[0][2])),
			Vector3::Length(Vector3(matrix.m[1][0], matrix.m[1][1], matrix.m[1][2])),
			Vector3::Length(Vector3(matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]))});
		// Boxの回転とScaleは視差補正にも反映する
		BakedReflectionCapture result{matrix.GetTranslationValue(), capture.radius * scale,
			capture.extents, capture.shape, 0, 0, 0, capture.intensity};
		const Matrix4x4 inverse = Matrix4x4::Inverse(matrix);
		for (uint32_t row = 0; row < 4; ++row) std::copy_n(inverse.m[row], 4, result.worldToBox[row].begin());
		input.preparation.data.captures.push_back(result);
	});
	world.ForEach<SkyboxRendererComponent, SceneObjectComponent>([&](Entity, const SkyboxRendererComponent& skybox,
		const SceneObjectComponent& object) {
		if (input.skyboxTexture || !skybox.visible || !object.activeInHierarchy || object.sceneInstanceID != sceneInstance) return;
		input.skyboxTexture = skybox.cubemapTexture;
		input.skyboxColor = skybox.color;
		input.skyIntensity = skybox.iblIntensity;
		addAsset(input.skyboxTexture);
	});
	// 一時Worldだけライトを絞り、通常描画と同じ変換を使う
	RestrictLights<DirectionalLightComponent>(world, sceneInstance);
	RestrictLights<PointLightComponent>(world, sceneInstance);
	RestrictLights<SpotLightComponent>(world, sceneInstance);
	RestrictLights<RectLightComponent>(world, sceneInstance);
	FrameLightBatch batch;
	DirectionalLightExtractor{}.Extract(world, batch);
	PointLightExtractor{}.Extract(world, batch);
	SpotLightExtractor{}.Extract(world, batch);
	RectLightExtractor{}.Extract(world, batch);
	for (const auto& light : batch.GetDirectionalLights()) input.directionalLights.push_back(LightGPUConversion::ToGPU(light));
	for (const auto& light : batch.GetPointLights()) input.pointLights.push_back(LightGPUConversion::ToGPU(light));
	for (const auto& light : batch.GetSpotLights()) input.spotLights.push_back(LightGPUConversion::ToGPU(light));
	for (const auto& light : batch.GetRectLights()) input.rectLights.push_back(LightGPUConversion::ToGPU(light));
	// Materialの依存画像と外部モデルの付随ファイルも照合対象に含める
	const auto initialAssets = assets;
	auto addDependencies = [&](auto&& self, AssetID id) -> void {
		for (AssetID dependency : database.FindDependencies(id)) {
			if (assets.insert(dependency).second) self(self, dependency);
		}
	};
	for (AssetID id : initialAssets) addDependencies(addDependencies, id);
	for (AssetID id : assets) input.assets.emplace_back(id, database.ResolveFullPath(id));
	return input;
}
