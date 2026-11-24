#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

#include <memory>
#include <optional>

namespace Engine {

	enum class ClothPinMode : uint8_t { Edge, Corners, Fasteners };
	enum class ClothEdge : uint8_t { Left, Right, Top, Bottom };

	struct ClothSettings {
		bool enabled = true, playInEditMode = false;
		ClothPinMode pinMode = ClothPinMode::Edge;
		ClothEdge edge = ClothEdge::Left;
		int32_t fasteners = 5;
		float foldDepth = 0.0f;
		float stretchCompliance = 0.000001f, shearCompliance = 0.00001f, bendCompliance = 0.001f;
		float damping = 1.5f, gravityScale = 1.0f;
		bool windEnabled = true, receiveSceneWind = false;
		uint32_t windCollisionMask = UINT32_MAX;
		Vector3 windDirection{1.0f, 0.1f, 0.5f};
		float windStrength = 5.0f, gustStrength = 2.0f, gustFrequency = 0.8f;
		float thickness = 0.015f;
		uint32_t collisionMask = ~0u;
	};

	struct ClothConstraint {
		uint32_t a, b;
		uint8_t kind;
		float restLength, lambda;
	};

	class ClothGPUSolver;
	struct ClothGPUEnvironment;
	struct ClothGPUUpdate {
		Matrix4x4 world, inverse;
		std::shared_ptr<const ClothGPUEnvironment> environment;
		uint32_t steps = 0;
		double startTime = 0.0;
	};

	struct ClothRuntime {
		std::shared_ptr<ClothGPUSolver> gpu;
		std::optional<ClothGPUUpdate> pendingGPU;
		bool gpuUnavailable = false;
		PrimitiveMeshData mesh;
		PrimitiveGeometryKey key;
		std::vector<Vector3> rest, positions, previous, acceleration;
		std::vector<uint8_t> pinned;
		std::vector<ClothConstraint> constraints;
		Vector3 scale{}, boundsCenter{};
		float boundsRadius = 0.0f;
		uint64_t revision = 0;
		double accumulator = 0.0, time = 0.0;
		bool editing = false;
	};

	struct ClothRecord {
		ClothSettings settings;
		ClothRuntime runtime;
		UUID owner{};
	};
	struct ClothStorageTag;
	using ClothStorage = GenerationalPool<ClothRecord, ClothStorageTag>;

	struct ClothComponent {
		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::Render;
		ClothStorage::Handle handle{};
		static void OnAdded(ECSWorld&, const Entity&, ClothComponent&);
		static void OnRemoved(ECSWorld&, const Entity&);
		static void InitializeStorage(ECSWorld&, const Entity&, ClothComponent&);
		static void ReleaseStorage(ECSWorld&, const Entity&, ClothComponent&);
		static void CloneStorage(const ECSWorld&, ECSWorld&, const Entity&, const ClothComponent&, ClothComponent&);
		static void DeserializeECS(ECSWorld&, const Entity&, const nlohmann::json&, ClothComponent&);
		static void SerializeECS(const ECSWorld&, const Entity&, const ClothComponent&, nlohmann::json&);
	};

	bool IsClothPlane(const PrimitiveRendererComponent* renderer);
	void from_json(const nlohmann::json& in, ClothSettings& settings);
	void to_json(nlohmann::json& out, const ClothSettings& settings);
	ClothRecord* TryGetCloth(ECSWorld& world, Entity owner);
	const ClothRecord* TryGetCloth(const ECSWorld& world, Entity owner);
	void SetClothSettings(ECSWorld& world, Entity owner, const ClothSettings& settings);
	void ResetCloth(ECSWorld& world, Entity owner);
}
