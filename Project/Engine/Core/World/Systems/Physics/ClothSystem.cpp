#include "ClothSystem.h"

//============================================================================
//	include
//============================================================================
#include "ClothCollision.h"
#include <Engine/Core/Rendering/Primitive/ClothGPUEnvironment.h>
#include <Engine/Core/Rendering/Primitive/ClothGPUSolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include "ClothWind.h"
#include <Engine/Core/Rendering/Particle/ParticleFieldAssetCache.h>
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

using namespace Engine;

namespace {

	constexpr double kStep = 1.0 / 120.0;
	constexpr float kDt = static_cast<float>(kStep);

	bool IsPinned(const ClothSettings& settings, int x, int y, int nx, int ny) {

		const bool side = settings.edge == ClothEdge::Left || settings.edge == ClothEdge::Right;
		const bool onEdge = side ? x == (settings.edge == ClothEdge::Left ? 0 : nx) :
			y == (settings.edge == ClothEdge::Bottom ? 0 : ny);
		if (!onEdge) return false;
		if (settings.pinMode == ClothPinMode::Edge) return true;
		const int coord = side ? y : x;
		const int end = side ? ny : nx;
		if (settings.pinMode == ClothPinMode::Corners) return coord == 0 || coord == end;
		const int intervals = std::clamp(settings.fasteners - 1, 1, end);
		const int slot = (coord * intervals + end / 2) / end;
		return coord == (slot * end + intervals / 2) / intervals;
	}

	void BuildGrid(ClothRuntime& r, const ClothSettings& s, const PrimitiveRendererComponent& renderer,
		const ResolvedWorldTransform& transform, bool editing) {

		const auto revision = r.revision;
		r = {};
		r.revision = revision;
		r.editing = editing;
		r.scale = transform.scale;
		r.key = PrimitiveMeshGenerator::ComputeKey(renderer);
		PrimitiveMeshGenerator::Generate(renderer, r.mesh);
		const int nx = std::clamp(renderer.plane.divideX, 1, kMaxPrimitiveDivide);
		const int ny = std::clamp(renderer.plane.divideY, 1, kMaxPrimitiveDivide);
		const int stride = nx + 1;
		const size_t count = r.mesh.vertices.size();
		r.rest.resize(count);
		r.pinned.resize(count);
		r.positions.resize(count);
		r.acceleration.resize(count);
		for (int y = 0; y <= ny; ++y) {
			for (int x = 0; x <= nx; ++x) {
				const int i = y * stride + x;
				const auto& v = r.mesh.vertices[i];
				const bool side = s.edge == ClothEdge::Left || s.edge == ClothEdge::Right;
				const float u = side ? float(y) / ny : float(x) / nx;
				const int folds = std::clamp(s.fasteners - 1, 1, side ? ny : nx);
				const float fold = s.foldDepth * std::sin(2.0f * std::numbers::pi_v<float> * folds * u);
				r.rest[i] = v.position + v.normal * fold;
				r.positions[i] = Vector3::Transform(r.rest[i], transform.matrix);
				r.pinned[i] = IsPinned(s, x, y, nx, ny);
			}
		}
		r.previous = r.positions;
		const auto link = [&](int a, int b, uint8_t kind) {
			r.constraints.push_back({uint32_t(a), uint32_t(b), kind, (r.positions[a] - r.positions[b]).Length(), 0.0f});
		};
		for (int y = 0; y <= ny; ++y) {
			for (int x = 0; x <= nx; ++x) {
				const int i = y * stride + x;
				if (x < nx) link(i, i + 1, 0);
				if (y < ny) link(i, i + stride, 0);
				if (x < nx && y < ny) {
					link(i, i + stride + 1, 1);
					link(i + 1, i + stride, 1);
				}
				if (x + 2 <= nx) link(i, i + 2, 2);
				if (y + 2 <= ny) link(i, i + 2 * stride, 2);
			}
		}
	}

	void Step(ClothRuntime& r, const ClothSettings& s, const Matrix4x4& matrix,
		std::span<const CollisionShapeInstance> colliders, std::span<const ClothWind::WindField> fields) {

		r.time += kStep;
		const float phase = float(r.time * s.gustFrequency * 2.0 * std::numbers::pi);
		const Vector3 direction = Vector3::NormalizeOr(s.windDirection, {});
		std::fill(r.acceleration.begin(), r.acceleration.end(), Vector3(0, -9.81f * s.gravityScale, 0));
		const auto& indices = r.mesh.indices;
		for (size_t i = 0; s.windEnabled && i < indices.size(); i += 3) {
			const auto a = indices[i], b = indices[i + 1], c = indices[i + 2];
			const Vector3 n = Vector3::NormalizeOr(Vector3::Cross(r.positions[b] - r.positions[a], r.positions[c] - r.positions[a]), {}, 1e-9f);
			const float gust = std::sin(phase + r.rest[a].x * 2.3f + r.rest[a].y * 1.7f);
			const Vector3 wind = direction * (s.windStrength + s.gustStrength * gust);
			const Vector3 velocity = ((r.positions[a] - r.previous[a]) + (r.positions[b] - r.previous[b]) +
				(r.positions[c] - r.previous[c])) / (3.0f * kDt);
			const float speed = std::clamp(Vector3::Dot(wind - velocity, n), -50.0f, 50.0f);
			const Vector3 force = n * (speed * std::abs(speed) / 6.0f);
			r.acceleration[a] += force;
			r.acceleration[b] += force;
			r.acceleration[c] += force;
		}

		if (s.receiveSceneWind) {
			for (const auto& field : fields) {
				if (!(field.mask & s.windCollisionMask)) continue;
				for (size_t i = 0; i < r.positions.size(); ++i) {
					if (!r.pinned[i]) r.acceleration[i] += ClothWind::SampleForce(field, r.positions[i], (r.positions[i] - r.previous[i]) / kDt, kDt);
				}
			}
		}

		const float drag = std::exp(-s.damping * kDt);
		for (size_t i = 0; i < r.positions.size(); ++i) {
			const auto p = r.positions[i];
			r.positions[i] = r.pinned[i] ? Vector3::Transform(r.rest[i], matrix) :
				p + (p - r.previous[i]) * drag + r.acceleration[i] * (kDt * kDt);
			r.previous[i] = p;
		}
		const float alpha[] = {s.stretchCompliance / (kDt * kDt), s.shearCompliance / (kDt * kDt), s.bendCompliance / (kDt * kDt)};
		for (auto& link : r.constraints) link.lambda = 0;
		for (int iteration = 0; iteration < 6; ++iteration) {
			for (auto& link : r.constraints) {
				const int wa = !r.pinned[link.a], wb = !r.pinned[link.b];
				if (wa + wb == 0) continue;
				const auto delta = r.positions[link.b] - r.positions[link.a];
				const float length = delta.Length();
				if (length < 1e-7f) continue;
				const float compliance = alpha[link.kind];
				const float change = (link.restLength - length - compliance * link.lambda) / (wa + wb + compliance);
				link.lambda += change;
				const auto correction = delta * (change / length);
				if (wa) r.positions[link.a] -= correction;
				if (wb) r.positions[link.b] += correction;
			}
			if (!colliders.empty()) {
				for (size_t i = 0; i < r.positions.size(); ++i) {
					if (!r.pinned[i]) ClothCollisionQuery::Project(colliders, r.positions[i], s.thickness);
				}
			}
		}
	}

	void Publish(ClothRuntime& r, const PrimitivePlaneParams& plane, const Matrix4x4& inverse) {

		auto& vertices = r.mesh.vertices;
		for (size_t i = 0; i < vertices.size(); ++i) vertices[i].position = Vector3::Transform(r.positions[i], inverse);
		const int nx = std::clamp(plane.divideX, 1, kMaxPrimitiveDivide);
		const int ny = std::clamp(plane.divideY, 1, kMaxPrimitiveDivide);
		const int stride = nx + 1;
		auto lo = vertices.front().position, hi = lo;
		for (int y = 0; y <= ny; ++y) {
			for (int x = 0; x <= nx; ++x) {
				const int i = y * stride + x;
				auto& v = vertices[i];
				const auto du = vertices[i + (x < nx)].position - vertices[i - (0 < x)].position;
				const auto dv = vertices[i + stride * (y < ny)].position - vertices[i - stride * (0 < y)].position;
				const float sign = plane.axis == PrimitivePlaneAxis::XZ ? -1.0f : 1.0f;
				v.normal = Vector3::NormalizeOr(Vector3::Cross(du, dv) * sign, v.normal, 1e-9f);
				v.tangent = Vector3::NormalizeOr(du - v.normal * Vector3::Dot(v.normal, du), v.tangent);
				v.tangentSign = -sign;
				lo.x = (std::min)(lo.x, v.position.x); hi.x = (std::max)(hi.x, v.position.x);
				lo.y = (std::min)(lo.y, v.position.y); hi.y = (std::max)(hi.y, v.position.y);
				lo.z = (std::min)(lo.z, v.position.z); hi.z = (std::max)(hi.z, v.position.z);
			}
		}
		r.boundsCenter = (lo + hi) * 0.5f;
		r.boundsRadius = (hi - r.boundsCenter).Length();
		++r.revision;
	}
}

ClothSystem::ClothSystem() = default;
ClothSystem::~ClothSystem() = default;

void ClothSystem::Update(ECSWorld& world, SystemContext& context) {

	std::vector<ClothCollisionQuery::Collider> candidates;
	std::vector<CollisionShapeInstance> colliders;
	bool collected = false;
	std::optional<std::vector<ClothWind::WindField>> wind;
	uint32_t selectedMask = 0;
	// 同じ更新で動く布は、場と衝突形状のスナップショットを共有する
	auto environment = std::shared_ptr<ClothGPUEnvironment>{};
	bool needsFields = false, needsColliders = false, usesWindAtlas = false;
	world.ForEach<ClothComponent>([&](Entity owner, ClothComponent& component) {
		auto* record = world.GetStorage().Get<ClothStorage>().TryGet(component.handle);
		if (!record) return;
		auto& r = record->runtime;
		const auto& s = record->settings;
		const auto* renderer = world.TryGetComponentForBinding<PrimitiveRendererComponent>(owner);
		const bool editing = context.mode == WorldMode::Edit;
		ResolvedWorldTransform transform;
		Matrix4x4 inverse;
		if (!s.enabled || !IsClothPlane(renderer) ||
			!IsEntityActiveInHierarchy(world, owner) || !TransformWorldUtility::ResolveWorldTransform(world, owner, transform) ||
			!Matrix4x4::TryInverse(transform.matrix, inverse)) {
			if (!r.mesh.vertices.empty()) ResetCloth(world, owner);
			return;
		}
		const bool rebuild = r.mesh.vertices.empty() || r.key != PrimitiveMeshGenerator::ComputeKey(*renderer) ||
			(r.scale - transform.scale).Length() > 0.0001f || editing != r.editing;
		if (rebuild) BuildGrid(r, s, *renderer, transform, editing);
		if (editing && !s.playInEditMode) {
			if (rebuild) {
				Publish(r, renderer->plane, inverse);
				world.MarkRenderDataModified(owner);
			}
			return;
		}
		usesWindAtlas |= context.graphicsPlatform && context.renderAssetLibrary && !r.gpuUnavailable && s.receiveSceneWind && s.windCollisionMask;
		const double dt = editing ? context.unscaledDeltaTime : context.deltaTime;
		r.accumulator += std::clamp(dt, 0.0, 0.1);
		if (r.accumulator + 1e-9 < kStep && !rebuild) return;
		if (context.graphicsPlatform && context.renderAssetLibrary && !r.gpuUnavailable) {
			if (!environment) environment = std::make_shared<ClothGPUEnvironment>();
			needsFields |= s.receiveSceneWind && s.windCollisionMask != 0;
			needsColliders |= s.collisionMask != 0;
			if (!r.pendingGPU) r.pendingGPU.emplace();
			auto& pending = *r.pendingGPU;
			if (pending.steps == 0) pending.startTime = r.time;
			pending.world = transform.matrix;
			pending.inverse = inverse;
			pending.environment = environment;
			const auto steps = static_cast<uint32_t>((r.accumulator + 1e-9) / kStep);
			pending.steps += steps;
			r.accumulator -= steps * kStep;
			r.time += steps * kStep;
			if (rebuild) {
				Publish(r, renderer->plane, inverse);
				// 固定辺から反対の端まで広がっても可視判定に含める
				const float largest = (std::max)({std::abs(r.scale.x), std::abs(r.scale.y), std::abs(r.scale.z)});
				const float smallest = (std::min)({std::abs(r.scale.x), std::abs(r.scale.y), std::abs(r.scale.z)});
				r.boundsRadius *= 4.0f * largest / (std::max)(smallest, 1e-6f);
				world.MarkRenderDataModified(owner);
			}
			return;
		}
		if (selectedMask != s.collisionMask) {
			if (!collected) {
				candidates = ClothCollisionQuery::Collect(world);
				collected = true;
			}
			ClothCollisionQuery::Select(candidates, s.collisionMask, colliders);
			selectedMask = s.collisionMask;
		}
		if (s.receiveSceneWind && s.windCollisionMask && !wind) wind = ClothWind::CollectWind(world, context.assetDatabase, fieldAssets_);
		while (kStep <= r.accumulator + 1e-9) {
			Step(r, s, transform.matrix, colliders, wind ? std::span<const ClothWind::WindField>(*wind) : std::span<const ClothWind::WindField>());
			r.accumulator -= kStep;
		}
		Publish(r, renderer->plane, inverse);
		if (rebuild) world.MarkRenderDataModified(owner);
	});
	if (environment) environment->Collect(world, context.assetDatabase, fieldAssets_, windAtlas_, needsFields, needsColliders);
	if (!usesWindAtlas) windAtlas_.reset();
}

void ClothSystem::DispatchGPU(ECSWorld& world, GraphicsCore& graphics, RenderAssetLibrary& assets, PipelineStateCache& pipelines) {

	world.ForEach<ClothComponent>([&](Entity owner, ClothComponent& component) {
		auto* record = world.GetStorage().Get<ClothStorage>().TryGet(component.handle);
		if (!record || !record->runtime.pendingGPU) return;
		auto& r = record->runtime;
		auto update = std::move(*r.pendingGPU);
		r.pendingGPU.reset();
		const auto* renderer = world.TryGetComponentForBinding<PrimitiveRendererComponent>(owner);
		if (!record->settings.enabled || !IsClothPlane(renderer) || !IsEntityActiveInHierarchy(world, owner)) return;
		if (!r.gpu) r.gpu = std::make_shared<ClothGPUSolver>();
		if (!r.gpu->Update(graphics, assets, pipelines, r, record->settings, renderer->plane, update.world,
			update.inverse, update.steps, update.startTime, *update.environment)) {
			r.gpu.reset();
			r.gpuUnavailable = true;
			Logger::Output(LogType::Engine, spdlog::level::warn, "ClothのGPU計算を初期化できないためCPUで更新します");
		}
		++r.revision;
	});
}
