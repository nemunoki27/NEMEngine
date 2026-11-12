#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Geometry/LogicPlacement.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

#include <string>

namespace Engine {

	class AssetDatabase;
	class PrefabBaseCache;

	struct LogicPlacementSource {

		UUID object{};
		AssetID prefab{};
	};

	enum class LogicPlacementOrientation : uint8_t { Original, Direction, Center };
	enum class LogicPlacementForwardAxis : uint8_t { PositiveX, PositiveY, PositiveZ, NegativeX, NegativeY, NegativeZ };

	struct LogicPlacementRequest {

		std::string parentName = "Object";
		LogicPlacementSource source;
		TransformComponent parentTransform;
		LogicPlacementSettings settings;
		LogicPlacementOrientation orientation = LogicPlacementOrientation::Original;
		LogicPlacementForwardAxis forward = LogicPlacementForwardAxis::PositiveZ;
		Vector3 rotationOffset{};
	};

	// シーンを変更せず配置元のルートを検証する
	bool ResolveLogicPlacementSource(ECSWorld& world, AssetDatabase* database, const LogicPlacementSource& source,
		bool is2D, TransformComponent& transform, std::string& diagnostic, PrefabBaseCache* cache = nullptr);
}
