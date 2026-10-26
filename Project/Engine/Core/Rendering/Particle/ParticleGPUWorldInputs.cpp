#include "ParticleGPUWorldInputs.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/ParticleFieldComponents.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

// c++
#include <algorithm>
#include <bit>
#include <cmath>

using namespace Engine;

namespace {

	// 世代を含めてWorld内のEntityを区別する
	uint64_t MakeKey(Entity entity) {

		return static_cast<uint64_t>(entity.generation) << 32 | entity.index;
	}

	// ベクトルに補助成分を加える
	Vector4 Pack(Vector3 value, float w = 0) {

		return Vector4(value.x, value.y, value.z, w);
	}

	// 格子の境界を単位立方体へ写す
	Matrix4x4 MakeGridTransform(const Matrix4x4& inverse, Vector3 minimum, Vector3 maximum) {

		return inverse * Matrix4x4::MakeTranslateMatrix(-minimum) *
			Matrix4x4::MakeScaleMatrix(Vector3::AnyInit(1) / (maximum - minimum));
	}

	// 前回姿勢からワールド空間の角速度を求める
	Vector3 MakeAngularVelocity(Quaternion previous, Quaternion current, float deltaTime) {

		if (deltaTime <= 0) return {};
		auto delta = Quaternion::Normalize(current * Quaternion::Inverse(previous));
		if (delta.w < 0) delta = -delta;
		const Vector3 axis(delta.x, delta.y, delta.z);
		const float sine = axis.Length();
		return sine > 0.000001f ? axis * (2 * std::atan2(sine, delta.w) / (sine * deltaTime)) : Vector3{};
	}
}

void ParticleGPUWorldInputCache::BeginFrame(const ECSWorld& world, float deltaTime, const ParticleGPUWorldLoaders& loaders) {

	frame_.forces.clear();
	frame_.vectorFields.clear();
	frame_.vectorSamples.clear();
	frame_.colliders.clear();
	frame_.heightmaps.clear();
	frame_.heightSamples.clear();
	frame_.diagnostic.clear();
	std::erase_if(vectorAssets_, [](const auto& entry) { return entry.second.owner.expired(); });
	std::erase_if(heightAssets_, [](const auto& entry) { return entry.second.owner.expired(); });
	deltaTime_ = deltaTime;
	std::unordered_map<uint64_t, Vector3> centers;
	std::unordered_map<uint64_t, Quaternion> rotations;
	std::unordered_map<AssetID, uint32_t> vectorOffsets;
	std::unordered_map<AssetID, uint32_t> heightOffsets;
	std::unordered_map<uint64_t, Matrix4x4> heightMatrices;
	world.ForEach<ForceFieldComponent, TransformComponent>([&](Entity entity, const ForceFieldComponent& component,
		const TransformComponent& transform) {
		if (!component.enabled || !IsEntityActiveInHierarchy(world, entity)) return;
		ParticleGPUForceField field;
		if (!Matrix4x4::TryInverse(transform.worldMatrix, field.worldToLocal)) return;
		field.parameters = Vector4(component.strength, component.range, component.falloff, static_cast<float>(component.type));
		field.axis = Pack(Vector3::NormalizeOr(Vector3::TransferNormal(component.axis, transform.worldMatrix), Vector3(0, 1, 0)));
		field.metadata[0] = component.layerMask;
		const auto center = transform.worldMatrix.GetTranslationValue();
		field.metadata[1] = std::bit_cast<uint32_t>(center.x);
		field.metadata[2] = std::bit_cast<uint32_t>(center.y);
		field.metadata[3] = std::bit_cast<uint32_t>(center.z);
		frame_.forces.push_back(field);
	});
	world.ForEach<VectorFieldComponent, TransformComponent>([&](Entity entity, const VectorFieldComponent& component,
		const TransformComponent& transform) {
		if (!component.enabled || !IsEntityActiveInHierarchy(world, entity)) return;
		Matrix4x4 inverse;
		if (!Matrix4x4::TryInverse(transform.worldMatrix, inverse)) return;
		ParticleGPUVectorField field;
		field.vectorToWorld = transform.worldMatrix;
		field.parameters = Vector4(component.intensity, component.tightness,
			static_cast<float>(component.preset), std::bit_cast<float>(component.layerMask));
		field.worldToGrid = MakeGridTransform(inverse, component.size * -0.5f, component.size * 0.5f);
		if (component.preset == ParticleVectorFieldPreset::Asset) {
			const auto data = loaders.vectorField ? loaders.vectorField(component.field) : nullptr;
			if (loaders.diagnostic && frame_.diagnostic.empty()) frame_.diagnostic = loaders.diagnostic(component.field);
			if (!data || data->vectors.empty()) {
				return;
			}
			const auto size = data->resolution;
			if (size.x < 1 || size.y < 1 || size.z < 1 || data->vectors.size() != static_cast<size_t>(size.x) * size.y * size.z) return;
			field.grid[0] = size.x; field.grid[1] = size.y; field.grid[2] = size.z;
			auto& cached = vectorAssets_[component.field];
			if (cached.owner.lock() != data) {
				cached.owner = data;
				cached.samples.clear();
				cached.samples.reserve(data->vectors.size());
				for (const auto& vector : data->vectors) cached.samples.push_back(Pack(vector));
			}
			const auto [offset, inserted] = vectorOffsets.try_emplace(component.field, static_cast<uint32_t>(frame_.vectorSamples.size()));
			field.grid[3] = offset->second;
			if (inserted) frame_.vectorSamples.insert(frame_.vectorSamples.end(), cached.samples.begin(), cached.samples.end());
		}
		frame_.vectorFields.push_back(field);
	});
	world.ForEach<CollisionComponent, TransformComponent>([&](Entity entity, const CollisionComponent& component,
		const TransformComponent& transform) {
		if (!component.enabled || !component.shape.enabled || !IsEntityActiveInHierarchy(world, entity)) return;
		if (!IsCollisionShape3D(component.shape.type)) return;
		ResolvedWorldTransform resolved;
		resolved.matrix = transform.worldMatrix;
		Vector3 position;
		if (!DecomposeAffine3D(resolved.matrix, position, resolved.rotation, resolved.scale)) return;
		const auto shape = CollisionShapeUtility::BuildShapeInstance(entity, component.shape, 0, resolved);
		ParticleGPUCollider collider;
		collider.centerRadius = Pack(shape.center, shape.radius);
		collider.axisXExtent = Pack(shape.axes[0], shape.halfExtents.x);
		collider.axisYExtent = Pack(shape.axes[1], shape.halfExtents.y);
		collider.axisZExtent = Pack(shape.axes[2], shape.halfExtents.z);
		collider.segmentStart = Pack(shape.segmentStart);
		collider.segmentEnd = Pack(shape.segmentEnd);
		const auto key = MakeKey(entity);
		if (const auto previous = previousCenters_.find(key); previous != previousCenters_.end() && deltaTime > 0)
			collider.velocity = Pack((shape.center - previous->second) / deltaTime);
		centers.emplace(key, shape.center);
		auto rotation = Quaternion::Identity();
		if (shape.type == ColliderShapeType::OBB3D || shape.type == ColliderShapeType::Capsule3D) {
			auto matrix = Matrix4x4::Identity();
			for (uint32_t row = 0; row < 3; ++row) {
				matrix.m[row][0] = shape.axes[row].x;
				matrix.m[row][1] = shape.axes[row].y;
				matrix.m[row][2] = shape.axes[row].z;
			}
			rotation = QuaternionFromRotationMatrixRowVector(matrix);
		} else if (shape.type == ColliderShapeType::Sphere3D && component.shape.useTransformRotation)
			rotation = resolved.rotation * Quaternion::FromEulerDegrees(component.shape.rotationDegrees);
		Vector3 angularVelocity;
		if (const auto previous = previousRotations_.find(key); previous != previousRotations_.end() && deltaTime > 0) {
			angularVelocity = MakeAngularVelocity(previous->second, rotation, deltaTime);
		}
		rotations.emplace(key, rotation);
		collider.metadata[0] = shape.type == ColliderShapeType::Sphere3D ? 1u : shape.type == ColliderShapeType::Capsule3D ? 3u : 2u;
		collider.metadata[1] = component.typeMask;
		collider.metadata[2] = entity.index;
		collider.metadata[3] = entity.generation;
		if (component.shape.isTrigger) collider.metadata[0] |= 0x80000000u;
		collider.segmentStart.w = angularVelocity.x;
		collider.segmentEnd.w = angularVelocity.y;
		collider.velocity.w = angularVelocity.z;
		frame_.colliders.push_back(collider);
	});
	previousCenters_ = std::move(centers);
	previousRotations_ = std::move(rotations);
	world.ForEach<HeightmapColliderComponent, TransformComponent>([&](Entity entity, const HeightmapColliderComponent& component,
		const TransformComponent& transform) {
		if (!component.enabled || !IsEntityActiveInHierarchy(world, entity)) return;
		const auto data = loaders.heightmap ? loaders.heightmap(component.heightmap) : nullptr;
		if (loaders.diagnostic && frame_.diagnostic.empty()) frame_.diagnostic = loaders.diagnostic(component.heightmap);
		if (!data || data->width < 2 || data->height < 2 || data->width > 4096 || data->height > 4096 ||
			data->samples.size() != static_cast<size_t>(data->width) * data->height) return;
		auto& cached = heightAssets_[component.heightmap];
		if (cached.owner.lock() != data) {
			cached.owner = data;
			cached.valid = std::none_of(data->samples.begin(), data->samples.end(), [](float value) { return !std::isfinite(value); });
		}
		if (!cached.valid) return;
		ParticleGPUHeightmap heightmap;
		if (!Matrix4x4::TryInverse(transform.worldMatrix, heightmap.worldToLocal)) return;
		heightmap.localToWorld = transform.worldMatrix;
		const auto key = MakeKey(entity);
		if (const auto previous = previousHeightmapMatrices_.find(key); previous != previousHeightmapMatrices_.end() && deltaTime > 0) {
			heightmap.velocity = Pack((transform.worldMatrix.GetTranslationValue() - previous->second.GetTranslationValue()) / deltaTime);
			Vector3 position, scale;
			Quaternion before, after;
			if (DecomposeAffine3D(previous->second, position, before, scale) && DecomposeAffine3D(transform.worldMatrix, position, after, scale))
				heightmap.angularVelocity = Pack(MakeAngularVelocity(before, after, deltaTime));
		}
		heightMatrices.emplace(key, transform.worldMatrix);
		heightmap.size = Pack(component.size);
		heightmap.grid[0] = data->width; heightmap.grid[1] = data->height;
		const auto [offset, inserted] = heightOffsets.try_emplace(component.heightmap, static_cast<uint32_t>(frame_.heightSamples.size()));
		heightmap.grid[2] = offset->second;
		heightmap.grid[3] = component.layerMask;
		heightmap.receiver[0] = entity.index; heightmap.receiver[1] = entity.generation;
		frame_.heightmaps.push_back(heightmap);
		if (inserted) frame_.heightSamples.insert(frame_.heightSamples.end(), data->samples.begin(), data->samples.end());
	});
	previousHeightmapMatrices_ = std::move(heightMatrices);
}

std::shared_ptr<const ParticleGPUWorldSnapshot> ParticleGPUWorldInputCache::Build(const ECSWorld& world, Entity owner,
	std::span<const SceneEntityReference> references, const ParticleCollisionSettings& collision, Entity mainGameCamera) {

	auto result = std::make_shared<ParticleGPUWorldSnapshot>(frame_);
	const auto* storage = world.GetStorage().TryGet<SplineStorage>();
	for (const auto& reference : references) {
		ParticleGPUSplineRange range;
		const auto entity = reference.Resolve(world, owner);
		const auto* component = world.TryGetComponent<SplineComponent>(entity);
		const auto* transform = world.TryGetComponent<TransformComponent>(entity);
		const auto* record = component && storage ? storage->TryGet(component->handle) : nullptr;
		if (record && transform) {
			auto& cached = splines_[MakeKey(entity)];
			if (cached.revision != record->revision || cached.transform != transform->worldMatrix) {
				cached.samples.clear();
				Vector3 previousTangent(1, 0, 0);
				Vector3 normal(0, 1, 0);
				for (const auto& sample : BuildSplineSamples(record->value, transform->worldMatrix)) {
					if (cached.samples.empty()) {
						const auto basis = std::abs(sample.tangent.y) < 0.99f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
						normal = Vector3::NormalizeOr(Vector3::Cross(basis, sample.tangent), Vector3(0, 0, 1));
					} else {
						const auto cross = Vector3::Cross(previousTangent, sample.tangent);
						const float cosine = std::clamp(Vector3::Dot(previousTangent, sample.tangent), -1.0f, 1.0f);
						if (cosine > -0.9999f) normal += Vector3::Cross(cross, normal) +
							Vector3::Cross(cross, Vector3::Cross(cross, normal)) / (1 + cosine);
						normal = Vector3::NormalizeOr(normal - sample.tangent * Vector3::Dot(normal, sample.tangent), normal);
					}
					cached.samples.push_back({Pack(sample.position, sample.distance), Pack(sample.tangent), Pack(normal)});
					previousTangent = sample.tangent;
				}
				if (record->value.closed && cached.samples.size() > 1 && cached.samples.back().positionDistance.w > 0) {
					const auto& first = cached.samples.front();
					const auto& last = cached.samples.back();
					const Vector3 firstNormal(first.normal.x, first.normal.y, first.normal.z);
					const Vector3 lastNormal(last.normal.x, last.normal.y, last.normal.z);
					const Vector3 tangent(first.tangent.x, first.tangent.y, first.tangent.z);
					const float twist = std::atan2(Vector3::Dot(tangent, Vector3::Cross(lastNormal, firstNormal)),
						Vector3::Dot(lastNormal, firstNormal));
					for (auto& sample : cached.samples) {
						const float angle = twist * sample.positionDistance.w / last.positionDistance.w;
						const Vector3 axis(sample.tangent.x, sample.tangent.y, sample.tangent.z);
						const Vector3 frameNormal(sample.normal.x, sample.normal.y, sample.normal.z);
						sample.normal = Pack(frameNormal * std::cos(angle) + Vector3::Cross(axis, frameNormal) * std::sin(angle));
					}
				}
				cached.revision = record->revision;
				cached.transform = transform->worldMatrix;
				cached.closed = record->value.closed;
			}
			range.offset = static_cast<uint32_t>(result->splineSamples.size());
			range.count = static_cast<uint32_t>(cached.samples.size());
			range.closed = cached.closed ? 1u : 0u;
			range.length = cached.samples.empty() ? 0 : cached.samples.back().positionDistance.w;
			result->splineSamples.insert(result->splineSamples.end(), cached.samples.begin(), cached.samples.end());
		}
		result->splineRanges.push_back(range);
	}
	if (!collision.sceneColliders) result->colliders.clear();
	if (!collision.includeTriggers) std::erase_if(result->colliders, [](const auto& value) { return (value.metadata[0] & 0x80000000u) != 0; });
	if (collision.plane) {
		ParticleGPUCollider plane;
		plane.centerRadius = Pack(collision.planeNormal * collision.planeDistance);
		plane.axisYExtent = Pack(collision.planeNormal);
		plane.metadata[1] = UINT32_MAX;
		plane.metadata[2] = UINT32_MAX;
		result->colliders.push_back(plane);
	}
	if (!collision.heightmap) result->heightmaps.clear();
	std::erase_if(result->colliders, [&](const auto& value) { return (value.metadata[1] & collision.layerMask) == 0; });
	BuildParticleGPUCollisionBVH(*result, deltaTime_);
	result->constants.collision = Vector4(collision.radius, collision.bounce, collision.friction, static_cast<float>(collision.response));
	result->constants.depthParameters.z = collision.depthThickness;
	result->constants.counts[0] = static_cast<uint32_t>(result->forces.size());
	result->constants.counts[1] = static_cast<uint32_t>(result->vectorFields.size());
	result->constants.counts[2] = static_cast<uint32_t>(result->collisionNodes.size());
	result->constants.counts[3] = static_cast<uint32_t>(result->heightmaps.size());
	result->constants.options[0] = collision.enabled ? 1u : 0u;
	result->constants.options[1] = collision.depth ? 1u : 0u;
	result->constants.options[2] = collision.layerMask;
	result->constants.options[3] = static_cast<uint32_t>(result->splineRanges.size());
	result->depth.camera = collision.depthCamera.localFileID || collision.depthCamera.sourceAsset ?
		collision.depthCamera.Resolve(world, owner) : mainGameCamera;
	result->depth.requested = collision.enabled && collision.depth && result->depth.camera.IsValid();
	// 削除された制御曲線のキャッシュはWorldの現世代だけを残す
	std::erase_if(splines_, [&](const auto& entry) {
		return !world.IsAlive(Entity{static_cast<uint32_t>(entry.first), static_cast<uint32_t>(entry.first >> 32)});
	});
	return result;
}
