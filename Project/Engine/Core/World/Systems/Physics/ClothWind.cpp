#include "ClothWind.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleFieldAssetCache.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace Engine::ClothWind {

	std::vector<WindField> CollectWind(ECSWorld& world, const AssetDatabase* database,
		std::unique_ptr<ParticleFieldAssetCache>& assets) {

		std::vector<WindField> fields;
		const auto collect = [&]<class T>(Entity entity, const T& component) {
			if (!component.enabled || !component.layerMask || !IsEntityActiveInHierarchy(world, entity)) return;
			ResolvedWorldTransform transform;
			if (!TransformWorldUtility::ResolveWorldTransform(world, entity, transform)) return;
			WindField f;
			ParticleFieldBounds bounds;
			if (!bounds.Build(component.shape, component.range, component.size, transform.matrix) ||
				!Matrix4x4::TryInverse(bounds.UnitToWorld(), f.inverse)) return;
			f.center = bounds.center;
			f.world = transform.matrix;
			f.mask = component.layerMask;
			f.shape = component.shape;
			if constexpr (std::is_same_v<T, ForceFieldComponent>) {
				f.forceType = component.type;
				f.axis = Vector3::NormalizeOr(Vector3::TransferNormal(component.axis, f.world), Vector3(0, 1, 0));
				f.strength = component.strength;
				f.falloff = component.falloff;
			} else {
				f.vector = true;
				f.preset = component.preset;
				f.strength = component.intensity;
				f.tightness = std::clamp(component.tightness, 0.0f, 1.0f);
				if (f.preset == ParticleVectorFieldPreset::Asset) {
					if (!database) return;
					if (!assets) assets = std::make_unique<ParticleFieldAssetCache>();
					f.grid = assets->MakeLoaders(*database).vectorField(component.field);
					if (!f.grid) return;
				}
			}
			fields.push_back(std::move(f));
		};
		world.ForEach<ForceFieldComponent>([&](Entity e, ForceFieldComponent& c) { collect(e, c); });
		world.ForEach<VectorFieldComponent>([&](Entity e, VectorFieldComponent& c) { collect(e, c); });
		return fields;
	}

	Vector3 SampleForce(const WindField& f, Vector3 position, Vector3 velocity, float deltaTime) {

		const auto local = Vector3::Transform(position, f.inverse);
		const float distance = f.shape == ParticleFieldShape::Sphere ? local.Length() :
			(std::max)({std::abs(local.x), std::abs(local.y), std::abs(local.z)});
		if (1.0f < distance) return {};
		if (f.vector) {
			Vector3 v(0, 0, 1);
			if (f.grid) {
				const auto uv = (local + Vector3::AnyInit(1)) * 0.5f;
				const auto extent = f.grid->maximum - f.grid->minimum;
				v = SampleVectorField(*f.grid, f.grid->minimum + Vector3(uv.x * extent.x, uv.y * extent.y, uv.z * extent.z));
			} else if (f.preset == ParticleVectorFieldPreset::Vortex) {
				v = Vector3::NormalizeOr(Vector3::Cross(Vector3(0, 1, 0), local), {});
			}
			const auto target = Vector3::NormalizeOr(Vector3::TransferNormal(v, f.world), {}) * (v.Length() * f.strength);
			const float follow = 1.0f - std::pow(1.0f - f.tightness, deltaTime * 60.0f);
			return target * (1.0f - follow) + (target - velocity) * (follow / deltaTime);
		}
		if (1.0f <= distance) return {};
		const auto radial = position - f.center;
		Vector3 dir = f.axis;
		switch (f.forceType) {
		case ParticleForceFieldType::Attraction: dir = Vector3::NormalizeOr(radial * -1.0f, {}); break;
		case ParticleForceFieldType::Repulsion: dir = Vector3::NormalizeOr(radial, {}); break;
		case ParticleForceFieldType::Vortex: dir = Vector3::NormalizeOr(Vector3::Cross(f.axis, radial), {}); break;
		default: break;
		}
		return dir * (f.strength * std::pow(1.0f - distance, (std::max)(0.0f, f.falloff)));
	}
}
