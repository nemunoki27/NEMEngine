#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

namespace Engine {

	//============================================================================
	//	Entity Callbacks
	//	C#側のEntityクラスから呼び出されるネイティブ実装
	//============================================================================

	int32_t ManagedScriptRuntime::IsAliveCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		// 指定されたエンティティが現在ワールドに存在し、生存しているか確認
		return (world && world->IsAlive(resolved)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::CopyNameCallback(ManagedNativeEntity entity, char* buffer, int32_t capacity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		// ワールドが無効なら空文字列を返す
		if (!world) {
			if (buffer && 0 < capacity) {
				buffer[0] = '\0';
			}
			return 0;
		}

		// 名前コンポーネントからエンティティ名を取得してマネージド側バッファへコピー
		NameComponent* name = world->TryGetComponent<NameComponent>(resolved);
		return CopyStringToBuffer(name ? name->name : std::string{}, buffer, capacity);
	}

	void ManagedScriptRuntime::SetNameCallback(ManagedNativeEntity entity, const char* name) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		// エンティティが無効なら何もしない
		if (!world || !world->IsAlive(resolved)) {
			return;
		}

		// 既にNameComponentがあれば即時反映する非構造的な値変更、無い場合の自動追加はarchetype移動でForEach走査を壊し得るためコマンドバッファへ積む
		const std::string newName = name ? std::string(name) : std::string{};
		if (NameComponent* nameComponent = world->TryGetComponent<NameComponent>(resolved)) {
			nameComponent->name = newName;
		} else {
			world->GetCommandBuffer().EnqueueSetNameEnsuringComponent(resolved, newName);
		}
	}

	int32_t ManagedScriptRuntime::GetActiveSelfCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world) {
			return 1;
		}

		// エンティティ自身の有効状態を返す、コンポーネントがなければデフォルト有効とみなす
		SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(resolved);
		return (!sceneObject || sceneObject->activeSelf) ? 1 : 0;
	}

	void ManagedScriptRuntime::SetActiveSelfCallback(ManagedNativeEntity entity, int32_t active) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		if (!world || !world->IsAlive(resolved)) {
			return;
		}

		// 既にSceneObjectComponentがあれば即時反映する値変更とアクティブ伝播は非構造、無い場合の自動追加は構造変更になるためコマンドバッファへ積む
		if (world->TryGetComponent<SceneObjectComponent>(resolved)) {
			SceneObjectUtility::SetActiveSelf(*world, resolved, active != 0);
		} else {
			world->GetCommandBuffer().EnqueueSetActiveSelfEnsuringComponent(resolved, active != 0);
		}
	}

	int32_t ManagedScriptRuntime::GetActiveInHierarchyCallback(ManagedNativeEntity entity) {
		ECSWorld* world = ResolveWorld(entity);
		const Entity resolved = ResolveEntity(entity);
		// 自身と親すべてが有効であるかを確認する、描画や更新の最終的な判断基準
		return (world && IsEntityActiveInHierarchy(*world, resolved)) ? 1 : 0;
	}

} // Engine
