#include "CreateEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// c++
#include <algorithm>

//============================================================================
//	CreateEntityCommand classMethods
//============================================================================
namespace {

	Engine::Entity FindParentEntity(Engine::ECSWorld& world, Engine::UUID id) {

		if (!id) {
			return Engine::Entity::Null();
		}
		return world.FindByUUID(id);
	}
	// エンティティの所属先を解決する
	void ResolveOwnerRuntimeState(const Engine::EditorCommandContext& context,
		Engine::ECSWorld& world, const Engine::Entity& parent,
		Engine::UUID& outSceneInstanceID, Engine::AssetID& outSourceAsset) {

		outSceneInstanceID = Engine::UUID{};
		outSourceAsset = Engine::AssetID{};

		// 親がある場合は親の所属先を最優先で使う
		if (parent.IsValid() && world.IsAlive(parent)) {

			outSceneInstanceID = Engine::SceneObjectUtility::GetSceneInstanceID(world, parent);
			if (const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(parent)) {
				outSourceAsset = sceneObject->sourceAsset;
			}
		}

		// 親から取れなければアクティブシーンを使う
		if (context.editorContext) {

			if (!outSceneInstanceID) {
				outSceneInstanceID = context.editorContext->activeSceneInstanceID;
			}
			if (!outSourceAsset) {
				outSourceAsset = context.editorContext->activeSceneAsset;
			}
		}
	}
	// 既存のルートエンティティの中で最大の兄弟順を返し1つも無ければ-1
	// ヒエラルキーはルートをsiblingOrderの昇順で並べているため、
	// 末尾に並べたい新規エンティティはこの値より大きい順番を持たせる
	int32_t FindMaxRootSiblingOrder(Engine::ECSWorld& world, const Engine::Entity& exclude) {

		int32_t maxOrder = -1;
		world.ForEachAliveEntity([&](Engine::Entity entity) {

			if (entity == exclude) {
				return;
			}
			if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
				return;
			}
			// 親が生存していないものだけがルート
			const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
			if (world.IsAlive(hierarchy.parent)) {
				return;
			}
			maxOrder = (std::max)(maxOrder, hierarchy.siblingOrder);
			});
		return maxOrder;
	}
}

Engine::CreateEntityCommand::CreateEntityCommand(const std::string& name, UUID parentStableUUID,
	EntityCreationPreset preset, Dimension dimension) :
	name_(name), parentStableUUID_(parentStableUUID), preset_(preset), dimension_(dimension) {
}

void Engine::CreateEntityCommand::ApplyPreset(ECSWorld& world, const Entity& entity, const Entity& parent) {

	if (preset_ == EntityCreationPreset::Empty) {
		return;
	}
	TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);
	if (transform) {
		transform->dimension = Dimension::Type2D;
	}

	bool hasCanvasAncestor = false;
	Entity ancestor = parent;
	while (world.IsAlive(ancestor)) {

		if (world.HasComponent<CanvasComponent>(ancestor)) {
			hasCanvasAncestor = true;
			break;
		}
		const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(ancestor);
		ancestor = hierarchy ? hierarchy->parent : Entity::Null();
	}
	if (preset_ == EntityCreationPreset::Canvas || !hasCanvasAncestor) {
		world.AddComponent<CanvasComponent>(entity);
	}

	if (preset_ == EntityCreationPreset::Canvas) {
		return;
	}

	// UI要素はCanvas直下または単独Canvasの場合に画面中央へ作る
	if (transform) {
		const CanvasComponent* canvas = nullptr;
		if (world.HasComponent<CanvasComponent>(entity)) {
			canvas = world.TryGetComponent<CanvasComponent>(entity);
		} else if (world.IsAlive(parent)) {
			canvas = world.TryGetComponent<CanvasComponent>(parent);
		}
		if (canvas) {
			transform->localPos.x = canvas->referenceResolution.x * 0.5f;
			transform->localPos.y = canvas->referenceResolution.y * 0.5f;
			MarkTransformSubtreeDirty(world, entity);
		}
	}

	switch (preset_) {
	case EntityCreationPreset::UIImage: {
		auto& sprite = world.AddComponent<SpriteRendererComponent>(entity);
		sprite.size = Vector2(256.0f, 256.0f);
		break;
	}
	case EntityCreationPreset::UIText: {
		auto& text = world.AddComponent<TextRendererComponent>(entity);
		text.text = "Text";
		text.pivot = Vector2::AnyInit(0.5f);
		break;
	}
	case EntityCreationPreset::UIImageButton: {
		auto& sprite = world.AddComponent<SpriteRendererComponent>(entity);
		sprite.size = Vector2(240.0f, 64.0f);
		world.AddComponent<UISelectableComponent>(entity);
		world.AddComponent<UIImageButtonComponent>(entity);
		break;
	}
	case EntityCreationPreset::UITextButton: {
		auto& text = world.AddComponent<TextRendererComponent>(entity);
		text.text = "Button";
		text.pivot = Vector2::AnyInit(0.5f);
		world.AddComponent<UISelectableComponent>(entity);
		world.AddComponent<UITextButtonComponent>(entity);
		break;
	}
	case EntityCreationPreset::UIProgress: {
		auto& primitive = world.AddComponent<PrimitiveRendererComponent>(entity);
		primitive.type = PrimitiveType::Plane;
		primitive.renderSpace = PrimitiveRenderSpace::Screen2D;
		primitive.plane.size = Vector2(320.0f, 32.0f);
		primitive.material = BuiltinAssets::Materials::ProgressPrimitive;
		primitive.blendMode = BlendMode::Normal;
		primitive.queue = RenderPhase::ScreenUI;
		world.AddComponent<UIProgressComponent>(entity);
		break;
	}
	case EntityCreationPreset::Empty:
	case EntityCreationPreset::Canvas:
	default:
		break;
	}
}

bool Engine::CreateEntityCommand::CreateInternal(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// Redoでも同じUUIDを使えるようにする
	Entity entity = world->CreateEntity(createdStableUUID_);
	if (!createdStableUUID_) {

		createdStableUUID_ = world->GetUUID(entity);
	}

	// デフォルトのコンポーネントを追加する
	SceneAuthoring::EnsureGameObjectDefaults(*world, entity, name_);
	if (TransformComponent* transform =
		world->TryGetComponent<TransformComponent>(entity)) {
		transform->dimension = dimension_;
	}

	// 親指定がある場合は親子付け
	Entity parent = parentStableUUID_ ? world->FindByUUID(parentStableUUID_) : Entity::Null();

	// 新規作成時点でシーン所属を決める
	UUID resolvedSceneInstanceID{};
	AssetID resolvedSourceAsset{};
	ResolveOwnerRuntimeState(context, *world, parent, resolvedSceneInstanceID, resolvedSourceAsset);

	// シーン所属の情報をコンポーネントに書き込む
	if (world->HasComponent<SceneObjectComponent>(entity)) {

		auto& sceneObject = world->GetComponent<SceneObjectComponent>(entity);

		if (resolvedSceneInstanceID) {
			sceneObject.sceneInstanceID = resolvedSceneInstanceID;
		}
		if (resolvedSourceAsset) {
			sceneObject.sourceAsset = resolvedSourceAsset;
		}
	}

	// 親指定がある場合は親子付け
	if (parent.IsValid() && world->IsAlive(parent)) {

		HierarchySystem hierarchySystem;
		hierarchySystem.SetParent(*world, entity, parent);
	} else if (world->HasComponent<HierarchyComponent>(entity)) {

		// ルート直下に作る場合は、ヒエラルキー上で末尾に並ぶよう兄弟順を最後にする
		auto& hierarchy = world->GetComponent<HierarchyComponent>(entity);
		hierarchy.siblingOrder = FindMaxRootSiblingOrder(*world, entity) + 1;
	}

	ApplyPreset(*world, entity, parent);

	if (context.editorState) {

		context.editorState->SelectEntity(entity);
	}
	return true;
}

bool Engine::CreateEntityCommand::Execute(EditorCommandContext& context) {

	return CreateInternal(context);
}

void Engine::CreateEntityCommand::Undo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !createdStableUUID_) {
		return;
	}

	// UUIDからエンティティを検索し、存在する場合は破棄する
	Entity created = world->FindByUUID(createdStableUUID_);
	if (world->IsAlive(created)) {

		EditorEntitySnapshotUtility::DestroySubtree(*world, created);
		context.RebuildHierarchyAll();
	}
	// 親エンティティが存在する場合は選択を更新する
	if (context.editorState) {

		Entity parent = world->FindByUUID(parentStableUUID_);
		context.editorState->SelectEntity((world->IsAlive(parent) ? parent : Entity::Null()));
	}
}

bool Engine::CreateEntityCommand::Redo(EditorCommandContext& context) {

	return CreateInternal(context);
}
