#include "AssetEntityFactory.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Editor/Assets/Importer/Font/MSDFFontGenerator.h>

// c++
#include <filesystem>
#include <string>

//============================================================================
//	AssetEntityFactory internalMethods
//============================================================================
namespace {

	// アセットパスから表示名を作る
	std::string MakeNameFromPath(const char* assetPath) {

		std::string stem = Engine::Algorithm::PathToUTF8(
			Engine::Algorithm::PathFromUTF8(assetPath).stem());
		return stem.empty() ? std::string("Entity") : stem;
	}

	// ルートエンティティをヒエラルキー上の末尾へ並べる、右クリック作成と同じ見え方にする
	void PlaceRootAtBottom(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return;
		}
		// 既存ルートの最大siblingOrderを調べ、その次の値を割り当てる
		int32_t maxOrder = -1;
		world.ForEachAliveEntity([&](Engine::Entity other) {

			if (other == entity || !world.HasComponent<Engine::HierarchyComponent>(other)) {
				return;
			}
			const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(other);
			// 親が生存していないものだけがルート
			if (world.IsAlive(hierarchy.parent)) {
				return;
			}
			if (hierarchy.siblingOrder > maxOrder) {
				maxOrder = hierarchy.siblingOrder;
			}
			});
		world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder = maxOrder + 1;
	}

	// 生成した単一エンティティに共通の初期化を施す
	Engine::Entity CreateBaseEntity(Engine::ECSWorld& world, const char* assetPath, Engine::UUID sceneInstanceID) {

		Engine::Entity entity = world.CreateEntity();
		Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		if (world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			world.GetComponent<Engine::SceneObjectComponent>(entity).sceneInstanceID = sceneInstanceID;
		}
		if (world.HasComponent<Engine::NameComponent>(entity)) {
			world.GetComponent<Engine::NameComponent>(entity).name = MakeNameFromPath(assetPath);
		}
		return entity;
	}
}

//============================================================================
//	AssetEntityFactory classMethods
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
		return true;
	default:
		return false;
	}
}

Engine::AssetSpawnResult Engine::AssetEntityFactory::Spawn(ECSWorld& world, AssetDatabase& database,
	GraphicsCore& graphicsCore, HierarchySystem& hierarchySystem,
	const EditorAssetDragDropPayload& payload, UUID sceneInstanceID) {

	AssetSpawnResult result{};

	switch (payload.assetType) {
	case AssetType::Mesh:
	{
		// モデルはMeshRendererを持つ3Dエンティティにする
		const Entity entity = CreateBaseEntity(world, payload.assetPath, sceneInstanceID);
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
	case AssetType::Texture:
	{
		// テクスチャはSpriteRendererを持つ2Dエンティティにする
		const Entity entity = CreateBaseEntity(world, payload.assetPath, sceneInstanceID);
		auto& renderer = world.AddComponent<SpriteRendererComponent>(entity);
		MaterialParameterValue texture{};
		texture.value = payload.assetID;
		renderer.materialInstance.Set(
			MaterialParameterIDs::BaseColorTexture,
			MaterialParameterNames::BaseColorTexture,
			MaterialParameterSemantic::BaseColorTexture,
			texture);

		// 初期サイズをテクスチャの実サイズに合わせる、未ロードなら既定サイズのままにする
		Vector2 textureSize{};
		if (RuntimeTextureResolver::TryResolveSize(graphicsCore, &database, payload.assetID, textureSize)) {
			renderer.size = textureSize;
		}

		result.root = entity;
		result.isThreeD = false;
		result.valid = true;
		break;
	}
	case AssetType::Font:
	{
		// フォントはTextRendererを持つ2Dエンティティにする、フォントを割り当て初期文字で表示する
		const Entity entity = CreateBaseEntity(world, payload.assetPath, sceneInstanceID);
		auto& renderer = world.AddComponent<TextRendererComponent>(entity);
		renderer.dimension = Dimension::Type2D;

		// ソースフォント(.ttf/.otf)はそのまま参照すると描画側でJSONとして読み込んで失敗するため、
		// インスペクターのドロップ時と同じくMSDFを生成して.font.jsonの参照へ寄せる
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
	case AssetType::Prefab:
	{
		// プレファブは展開してインスタンス化する、3D要素があれば3D扱いにする
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

	// 右クリック作成と同じく、生成したルートはヒエラルキーの末尾に並べる
	if (result.valid) {
		PlaceRootAtBottom(world, result.root);
	}
	return result;
}
