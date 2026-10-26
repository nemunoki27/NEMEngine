#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

namespace Engine {

	struct SplineControlPoint {

		Vector3 position{};
		Vector3 tangentIn{-0.333333f, 0, 0};
		Vector3 tangentOut{0.333333f, 0, 0};
	};

	struct SplineData {

		std::vector<SplineControlPoint> points{{}, {Vector3(1, 0, 0)}};
		bool closed = false;
		float tolerance = 0.002f;
	};

	struct SplineSample {

		Vector3 position{};
		float distance = 0;
		Vector3 tangent{1, 0, 0};
	};

	struct SplineRecord {

		SplineData value;
		UUID owner{};
		uint64_t revision = 1;
	};
	struct SplineStorageTag;
	using SplineStorage = GenerationalPool<SplineRecord, SplineStorageTag>;

	// 可変長制御点をWorldが所有し、チャンクには参照だけを保持する
	struct SplineComponent {

		static constexpr bool kHasECSHooks = true;
		SplineStorage::Handle handle{};

		static void OnAdded(ECSWorld& world, const Entity& entity, SplineComponent& component);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, SplineComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, SplineComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity, const nlohmann::json& in, SplineComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity, const SplineComponent& component, nlohmann::json& out);
		static void CloneStorage(const ECSWorld& sourceWorld, ECSWorld& destinationWorld,
			const Entity& entity, const SplineComponent& source, SplineComponent& destination);
	};
	static_assert(sizeof(SplineComponent) == 8);

	const SplineData* TryGetSpline(const ECSWorld& world, Entity entity);
	void SetSpline(ECSWorld& world, Entity entity, const SplineData& data);
	// ワールド空間の長さで誤差を抑えた弧長表を構築する
	std::vector<SplineSample> BuildSplineSamples(const SplineData& data, const Matrix4x4& world);
	SplineSample EvaluateSplineDistance(std::span<const SplineSample> samples, float distance, bool loop);
	void from_json(const nlohmann::json& in, SplineData& data);
	void to_json(nlohmann::json& out, const SplineData& data);
}
