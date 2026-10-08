#include "AssetEntityFactory.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Editor/Assets/Importer/Font/MSDFFontGenerator.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// c++
#include <filesystem>
#include <string>

//============================================================================
//	AssetEntityFactory internalMethods
//============================================================================
namespace {

	// アセットパスから表示名を作る
	std::string MakeNameFromPath(const char* assetPath) {

		std::string stem = Engine::Algorithm::PathToUTF8(Engine::Algorithm::PathFromUTF8(assetPath).stem());
		return stem.empty() ? std::string("Entity") : stem;
	}

	// ルートEntityをヒエラルキーの末尾へ並べる
	bool PlaceRootAtBottom(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return false;
		}
		// 既存ルートの末尾へ兄弟順を合わせる
		return Engine::HierarchyUtility::TryGetNextRootSiblingOrder(
			world, entity, world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder);
	}

	// 生成した単一エンティティに共通の初期化を施す
	Engine::Entity CreateBaseEntity(
		Engine::ECSWorld& world, const char* assetPath, Engine::UUID sceneInstanceID, Engine::Dimension dimension) {

		Engine::Entity entity = world.CreateEntity();
		Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		if (world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			world.GetComponent<Engine::SceneObjectComponent>(entity).sceneInstanceID = sceneInstanceID;
		}
		if (world.HasComponent<Engine::NameComponent>(entity)) {
			world.GetComponent<Engine::NameComponent>(entity).name = MakeNameFromPath(assetPath);
		}
		if (world.HasComponent<Engine::TransformComponent>(entity)) {
			world.GetComponent<Engine::TransformComponent>(entity).dimension = dimension;
		}
		return entity;
	}

	// Effectの描画空間から変換の次元を決める
	Engine::Dimension ResolveParticleEffectDimension(Engine::AssetDatabase& database, Engine::AssetID effectID) {

		const std::filesystem::path path = database.ResolveFullPath(effectID);
		if (path.empty()) {
			return Engine::Dimension::Type3D;
		}
		Engine::ParticleEffectAsset effect{};
		if (!Engine::FromJson(Engine::JsonAdapter::Load(path, false), effect)) {
			return Engine::Dimension::Type3D;
		}
		return effect.space == Engine::PrimitiveRenderSpace::Screen2D ? Engine::Dimension::Type2D : Engine::Dimension::Type3D;
	}
}

//============================================================================
//	AssetEntityFactory namespaceMethods
//============================================================================
bool Engine::AssetEntityFactory::CanSpawn(const EditorAssetDragDropPayload& payload) {

	// ディレクトリは対象外
	if (payload.isDirectory) {
		return false;
	}
	switch (payload.assetType) {
	case AssetType::Mesh:
	case AssetType::Texture:
	case AssetType::Prefab:
	case AssetType::Font:
	case AssetType::ParticleEffect:
		return true;
	default:
		return false;
	}
}

Engine::AssetSpawnResult Engine::AssetEntityFactory::Spawn(ECSWorld& world, AssetDatabase& database, GraphicsCore& graphicsCore,
	HierarchySystem& hierarchySystem, const EditorAssetDragDropPayload& payload, UUID sceneInstanceID) {

	AssetSpawnResult result{};
	if (!CanSpawn(payload)) {
		return result;
	}
	// 初期化と配置が終わるまで生成物を保持する
	SceneCreationScope creation(world);

	switch (payload.assetType) {
	case AssetType::Mesh: {
		// モデルはMeshRendererを持つ3Dエンティティにする
		const Entity entity = CreateBaseEntity(world, payload.assetPath, sceneInstanceID, Dimension::Type3D);
		auto& renderer = world.AddComponent<MeshRendererComponent>(entity);
		renderer.mesh = payload.assetID;
		renderer.material = {};
		renderer.queue = RenderPhase::Opaque;
		renderer.visible = true;
		renderer.enableZPrepass = true;
		MeshSubMeshAuthoring::SyncEntity(&database, world, entity, false);

		result.root = entity;
		result.isThreeD = true;
		result.valid = true;
		break;
	}
	case AssetType::Texture: {
		// テクスチャはSpriteRendererを持つ2Dエンティティにする
		const Entity entity = CreateBaseEntity(world, payload.assetPath, sceneInstanceID, Dimension::Type2D);
		auto& renderer = world.AddComponent<SpriteRendererComponent>(entity);
		MaterialParameterValue texture{};
		texture.value = payload.assetID;
		renderer.materialInstance.Set(MaterialParameterIDs::BaseColorTexture, MaterialParameterNames::BaseColorTexture,
			MaterialParameterSemantic::BaseColorTexture, texture);

		// 読込済みTextureの実サイズを初期サイズにする
		Vector2 textureSize{};
		if (RuntimeTextureResolver::TryResolveSize(graphicsCore, &database, payload.assetID, textureSize)) {
			renderer.size = textureSize;
		}

		result.root = entity;
		result.isThreeD = false;
		result.valid = true;
		break;
	}
	case AssetType::Font: {
		// FontはTextRendererを持つ2Dエンティティにする
		const Entity entity = CreateBaseEntity(world, payload.assetPath, sceneInstanceID, Dimension::Type2D);
		auto& renderer = world.AddComponent<TextRendererComponent>(entity);
		renderer.dimension = Dimension::Type2D;

		// 元FontからMSDFを生成して描画用の参照を設定する
		AssetID fontAsset = payload.assetID;
		const std::filesystem::path sourcePath = database.ResolveFullPath(payload.assetID);
		if (MSDFFontGenerator::IsFontSourceExtension(sourcePath)) {

			const MSDFFontGenerator::Result generated = MSDFFontGenerator::EnsureGenerated(database, sourcePath, false);
			fontAsset = generated.success ? generated.fontAssetID : AssetID{};
		}
		renderer.font = fontAsset;

		result.root = entity;
		result.isThreeD = false;
		result.valid = true;
		break;
	}
	case AssetType::ParticleEffect: {
		// エフェクトはParticleSystemを持つ単一エンティティにする
		const Dimension dimension = ResolveParticleEffectDimension(database, payload.assetID);
		const Entity entity = CreateBaseEntity(world, payload.assetPath, sceneInstanceID, dimension);
		auto& particleSystem = world.AddComponent<ParticleSystemComponent>(entity);
		particleSystem.effect = payload.assetID;

		result.root = entity;
		result.isThreeD = dimension == Dimension::Type3D;
		result.valid = true;
		break;
	}
	case AssetType::Prefab: {
		// Prefabを展開して3D要素の有無を調べる
		PrefabSystem prefabSystem{};
		PrefabInstantiateResult instantiateResult{};
		PrefabInstantiateDesc desc{};
		desc.ownerSceneInstanceID = sceneInstanceID;
		// 新規生成なのでルート名を.prefabのベース名にする
		desc.renameRootToPrefabName = true;
		if (prefabSystem.InstantiatePrefab(database, hierarchySystem, world, payload.assetID, instantiateResult, desc) &&
			world.IsAlive(instantiateResult.root)) {

			bool hasThreeD = false;
			for (const Entity& entity : instantiateResult.createdEntities) {
				if (world.IsAlive(entity) && world.HasComponent<MeshRendererComponent>(entity)) {
					hasThreeD = true;
					break;
				}
			}
			result.root = instantiateResult.root;
			result.isThreeD = hasThreeD;
			result.valid = true;
		}
		break;
	}
	default:
		break;
	}

	// 生成したルートをヒエラルキーの末尾へ並べる
	if (result.valid) {
		if (!PlaceRootAtBottom(world, result.root)) {
			return {};
		}
		creation.Commit();
	}
	return result;
}
