#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <memory>

namespace Engine {

	class PendingComponent;

	// コマンド種別
	enum class WorldCommandKind : uint8_t {

		DestroyEntity,
		AddComponentByName,
		AddComponentValue,
		RemoveComponentByName,
		RemoveScript,
		SetNameEnsuringComponent,
		SetActiveSelfEnsuringComponent,
		SetParent,
		CreateEntity,
		LoadSceneAdditive,
		LoadSceneSingle,
		UnloadScene,
	};

	// 1件の要求と共有する予約値を保持する
	struct WorldCommand {

		WorldCommandKind kind;			// 適用する操作
		Entity target = Entity::Null(); // 操作対象
		Entity parent = Entity::Null(); // 生成と親子変更の親
		bool boolValue = false;			// 有効状態かWorld姿勢の維持
		// 読み込むSceneと操作するInstance
		AssetID assetID{};
		UUID sceneInstanceID{};
		UUID scriptSlotID{}; // 削除するScriptの保存slot
		// Componentの型名かEntity名
		std::string text;
		// 追加前の読み書きで共有する値
		std::shared_ptr<PendingComponent> component;
	};

}
