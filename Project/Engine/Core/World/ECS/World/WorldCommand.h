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

namespace Engine {

	// コマンド種別
	enum class WorldCommandKind : uint8_t {

		DestroyEntity,
		AddComponentByName,
		RemoveComponentByName,
		SetNameEnsuringComponent,
		SetActiveSelfEnsuringComponent,
		SetParent,
		CreateEntity,
		LoadSceneAdditive,
		LoadSceneSingle,
		UnloadScene,
	};

	// transform stagingのどの成分が指定されたか
	enum WorldCommandFlags : uint8_t {

		FlagWorldPositionStays = 1 << 0,
		FlagHasPosition = 1 << 1,
		FlagHasRotation = 1 << 2,
		FlagHasScale = 1 << 3,
	};

	// 1コマンド分のデータで値はすべてコピー保持する
	struct WorldCommand {

		WorldCommandKind kind;
		Entity target = Entity::Null();
		Entity parent = Entity::Null();
		bool boolValue = false;
		uint8_t flags = 0;
		// Sceneのasset、Scene instanceのUUID
		AssetID assetID{};
		UUID sceneInstanceID{};
		// CreateEntityの初期SRTでstagingされた値を保持する
		Vector3 position{};
		Quaternion rotation = Quaternion::Identity();
		Vector3 scale = Vector3::AnyInit(1.0f);
		// AddComponent/RemoveComponent/SetName/CreateEntity(name)用の文字列
		std::string text;
	};

} // Engine
