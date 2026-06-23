#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	Query Callbacks
	//	C#側のTag公開とLayerマスク公開とEntity検索のネイティブ実装
	//============================================================================

	int32_t ManagedScriptRuntime::CopyTagCallback(ManagedNativeEntity entity, char* buffer, int32_t capacity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			if (buffer && 0 < capacity) {
				buffer[0] = '\0';
			}
			return 0;
		}

		// SceneObjectComponentにタグを持たせている、無ければUntagged扱い
		SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(resolved);
		return CopyStringToBuffer(sceneObject ? sceneObject->tag : std::string("Untagged"), buffer, capacity);
	}

	void ManagedScriptRuntime::SetTagCallback(ManagedNativeEntity entity, const char* tag) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}

		// tagは非構造的な値変更なので即時反映する、SceneObjectが無ければ何もしない
		if (SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(resolved)) {
			sceneObject->tag = tag ? std::string(tag) : std::string("Untagged");
		}
	}

	int32_t ManagedScriptRuntime::GetVisibilityLayerMaskCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return 0;
		}

		// 描画カリング用のマスク、カメラのcullingMaskと照合される
		SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(resolved);
		return sceneObject ? static_cast<int32_t>(sceneObject->visibilityLayerMask) : 0;
	}

	void ManagedScriptRuntime::SetVisibilityLayerMaskCallback(ManagedNativeEntity entity, int32_t mask) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		if (SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(resolved)) {
			sceneObject->visibilityLayerMask = static_cast<uint32_t>(mask);
		}
	}

	int32_t ManagedScriptRuntime::GetCollisionTypeMaskCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return 0;
		}

		// 衝突フィルタ用のタイプビットマスク、CollisionManagerのマトリクスと照合される
		CollisionComponent* collision = world->TryGetComponent<CollisionComponent>(resolved);
		return collision ? static_cast<int32_t>(collision->typeMask) : 0;
	}

	void ManagedScriptRuntime::SetCollisionTypeMaskCallback(ManagedNativeEntity entity, int32_t mask) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}
		if (CollisionComponent* collision = world->TryGetComponent<CollisionComponent>(resolved)) {
			collision->typeMask = static_cast<uint32_t>(mask);
		}
	}

	ManagedNativeEntity ManagedScriptRuntime::FindEntityByNameCallback(const char* name) {
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || !name) {
			return MakeNullNativeEntity();
		}

		// 名前一致の最初の1件を返す、走査順はEntity index順
		const std::string target(name);
		Entity found = Entity::Null();
		world->ForEach<NameComponent>([&](Entity entity, NameComponent& nameComponent) {
			if (!found.IsValid() && nameComponent.name == target) {
				found = entity;
			}
			});
		return found.IsValid() ? MakeNativeEntity(*world, found) : MakeNullNativeEntity();
	}

	ManagedNativeEntity ManagedScriptRuntime::FindEntityByTagCallback(const char* tag) {
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || !tag) {
			return MakeNullNativeEntity();
		}

		const std::string target(tag);
		Entity found = Entity::Null();
		world->ForEach<SceneObjectComponent>([&](Entity entity, SceneObjectComponent& sceneObject) {
			if (!found.IsValid() && sceneObject.tag == target) {
				found = entity;
			}
			});
		return found.IsValid() ? MakeNativeEntity(*world, found) : MakeNullNativeEntity();
	}

	int32_t ManagedScriptRuntime::FindEntitiesByTagCallback(const char* tag, ManagedNativeEntity* buffer, int32_t capacity) {
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || !tag) {
			return 0;
		}

		// bufferがnullやcapacity不足でも総数は数える、C#側はlength-queryで再取得する
		const std::string target(tag);
		int32_t count = 0;
		world->ForEach<SceneObjectComponent>([&](Entity entity, SceneObjectComponent& sceneObject) {
			if (sceneObject.tag != target) {
				return;
			}
			if (buffer && count < capacity) {
				buffer[count] = MakeNativeEntity(*world, entity);
			}
			++count;
			});
		return count;
	}

	ManagedNativeEntity ManagedScriptRuntime::FindEntityByComponentCallback(int32_t typeId) {
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || typeId < 0) {
			return MakeNullNativeEntity();
		}

		// compact type idから登録名を引き、名前ベースのHasComponentで走査する
		ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeId) >= registry.GetComponentTypeCount()) {
			return MakeNullNativeEntity();
		}
		const std::string typeName = registry.GetInfo(static_cast<uint32_t>(typeId)).name;

		Entity found = Entity::Null();
		world->ForEachAliveEntity([&](Entity entity) {
			if (!found.IsValid() && world->HasComponent(entity, typeName)) {
				found = entity;
			}
			});
		return found.IsValid() ? MakeNativeEntity(*world, found) : MakeNullNativeEntity();
	}

	int32_t ManagedScriptRuntime::FindEntitiesByComponentCallback(int32_t typeId, ManagedNativeEntity* buffer, int32_t capacity) {
		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || typeId < 0) {
			return 0;
		}

		ComponentTypeRegistry& registry = ComponentTypeRegistry::GetInstance();
		if (static_cast<uint32_t>(typeId) >= registry.GetComponentTypeCount()) {
			return 0;
		}
		const std::string typeName = registry.GetInfo(static_cast<uint32_t>(typeId)).name;

		int32_t count = 0;
		world->ForEachAliveEntity([&](Entity entity) {
			if (!world->HasComponent(entity, typeName)) {
				return;
			}
			if (buffer && count < capacity) {
				buffer[count] = MakeNativeEntity(*world, entity);
			}
			++count;
			});
		return count;
	}
} // Engine
