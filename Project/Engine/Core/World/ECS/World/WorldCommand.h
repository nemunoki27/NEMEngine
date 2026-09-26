#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

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
		SetNameEnsuringComponent,
		SetActiveSelfEnsuringComponent,
		SetParent,
		CreateEntity,
		LoadSceneAdditive,
		LoadSceneSingle,
		UnloadScene,
	};

	// 1コマンド分のデータで値はすべてコピー保持する
	struct WorldCommand {

		WorldCommandKind kind;
		Entity target = Entity::Null();
		Entity parent = Entity::Null();
		bool boolValue = false;
		// Sceneのasset、Scene instanceのUUID
		AssetID assetID{};
		UUID sceneInstanceID{};
		// AddComponent/RemoveComponent/SetName/CreateEntity(name)用の文字列
		std::string text;
		// 追加前の読み書きで共有する値
		std::shared_ptr<PendingComponent> component;
	};

} // Engine
