#include "DynamicGIPass.h"

//============================================================================
//	include
//============================================================================
#include "RenderFeatureResourceUtility.h"
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Utility/ScopedValue.h>
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingViewBufferSet.h>
#include <Engine/Core/Rendering/Renderer/Lighting/AtmosphereLighting.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightGPUConversion.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneGeometryUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>

#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <cfloat>
#include <tuple>

namespace {

	using namespace Engine;

	constexpr Engine::UUID kCachePass{0x4749434143484500ull};
	constexpr Engine::UUID kTracePass{0x4749545241434500ull};
	constexpr Engine::UUID kInterpolatePass{0x4749494e54455250ull};
	constexpr Engine::UUID kResolvePass{0x47495245534f4c56ull};

	uint32_t ProbeUpdatePeriod(DynamicGIQuality quality) {

		return quality == DynamicGIQuality::Low ? 12u : quality == DynamicGIQuality::High ? 6u : 8u;
	}

	uint32_t GIHash(uint32_t value) {

		value ^= value >> 16;
		value *= 0x7feb352du;
		value ^= value >> 15;
		value *= 0x846ca68bu;
		return value ^ (value >> 16);
	}
	struct BrickHash {
		uint32_t operator()(const std::array<int32_t, 3>& key) const {

			return GIHash(uint32_t(key[0]) ^ GIHash(uint32_t(key[1]) ^ GIHash(uint32_t(key[2]))));
		}
	};

	RenderFeaturePassSettings MakePass(Engine::UUID id, const char* name, AssetID material) {

		RenderFeaturePassSettings pass{};
		pass.id = id;
		pass.name = name;
		pass.anchor = RenderFeatureAnchor::BeforeLighting;
		pass.material = material;
		return pass;
	}

	float OutputScale(uint32_t outputSize, uint32_t viewSize) {

		// RenderTargetSizingが整数へ切り捨てる前の丸め誤差を吸収する
		return std::nextafter(static_cast<float>(outputSize) / viewSize, INFINITY);
	}

	RenderFeatureProfileAsset BuildProfile(DynamicGIQuality quality, uint32_t mask, uint32_t width, uint32_t height) {

		RenderFeatureProfileAsset profile{};
		const uint32_t spacing = quality == DynamicGIQuality::Low ? 12u : quality == DynamicGIQuality::High ? 4u : 8u;
		const uint32_t rayCount = quality == DynamicGIQuality::Low ? 4u : quality == DynamicGIQuality::High ? 32u : 8u;
		const uint32_t gridSize = quality == DynamicGIQuality::High ? 32u : 16u;
		auto configureTrace = [&](RenderFeaturePassSettings& pass) {
			pass.parameterOverrides.Set("rayCount", MaterialParameterValue{.value = rayCount});
			pass.parameterOverrides.Set("cullingMask", MaterialParameterValue{.value = mask});
			pass.parameterOverrides.Set("ambientIntensity", MaterialParameterValue{.value = 0.03f});
		};
		auto cache = MakePass(kCachePass, "GI World Cache", BuiltinAssets::Materials::GICache);
		cache.sourceKind = RenderFeatureSourceKind::SceneColor;
		cache.outputs.push_back({.name = "Color", .shaderResource = "gDestColor", .format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = OutputScale(6u * gridSize, width), .heightScale = OutputScale(gridSize * gridSize, height),
			.history = true, .historyShaderResource = "gHistoryColor"});
		cache.outputs.push_back({.name = "Moments", .shaderResource = "gDestMoments", .format = RenderFeatureTextureFormat::RG16_FLOAT,
			.widthScale = OutputScale(12u * gridSize, width), .heightScale = OutputScale(2u * gridSize * gridSize, height),
			.history = true, .historyShaderResource = "gHistoryMoments"});
		cache.parameterOverrides.Set("updatePeriod", MaterialParameterValue{.value =
			ProbeUpdatePeriod(quality)});
		configureTrace(cache);
		profile.passes.emplace_back(std::move(cache));

		auto trace = MakePass(kTracePass, "GI Probe Trace", BuiltinAssets::Materials::GIGather);
		trace.passInputs["gWorldCache"] = {kCachePass, "Color"};
		trace.passInputs["gWorldMoments"] = {kCachePass, "Moments"};
		trace.sceneInputs = {{"gSourcePosition", RenderTargetNames::kScenePositionMain}, {"gSourceNormal", RenderTargetNames::kSceneNormalMain},
			{"gSourceFlags", RenderTargetNames::kSceneFlagsMain}};
		trace.outputs.push_back({.name = "Color", .shaderResource = "gDestColor", .format = RenderFeatureTextureFormat::RGBA16_FLOAT,
			.widthScale = OutputScale((width + spacing - 1u) / spacing, width),
			.heightScale = OutputScale((height + spacing - 1u) / spacing, height)});
		configureTrace(trace);
		profile.passes.emplace_back(std::move(trace));

		auto interpolate = MakePass(kInterpolatePass, "GI Interpolation", BuiltinAssets::Materials::GIInterpolate);
		interpolate.sourceKind = RenderFeatureSourceKind::PassOutput;
		interpolate.source = {kTracePass, "Color"};
		interpolate.sceneInputs = {{"gSourcePosition", RenderTargetNames::kScenePositionMain}, {"gSourceNormal", RenderTargetNames::kSceneNormalMain},
			{"gSourceFlags", RenderTargetNames::kSceneFlagsMain}};
		interpolate.outputs.push_back({.name = "Color", .shaderResource = "gDestColor", .format = RenderFeatureTextureFormat::RGBA32_FLOAT});
		configureTrace(interpolate);
		profile.passes.emplace_back(std::move(interpolate));

		auto resolve = MakePass(kResolvePass, "GI Reconstruction", BuiltinAssets::Materials::GIResolve);
		resolve.sourceKind = RenderFeatureSourceKind::PassOutput;
		resolve.source = {kInterpolatePass, "Color"};
		resolve.passInputs["gWorldCache"] = {kCachePass, "Color"};
		resolve.passInputs["gWorldMoments"] = {kCachePass, "Moments"};
		resolve.sceneInputs = {{"gSourcePosition", RenderTargetNames::kScenePositionMain}, {"gSourceNormal", RenderTargetNames::kSceneNormalMain}};
		resolve.outputs.push_back({.name = "Color", .shaderResource = "gDestColor", .format = RenderFeatureTextureFormat::RGBA16_FLOAT});
		configureTrace(resolve);
		profile.passes.emplace_back(std::move(resolve));
		return profile;
	}
}

Engine::DynamicGIPass::DynamicGIPass(const RenderPipelineDeps& deps)
	: executor_(deps, RenderFeatureAnchor::BeforeLighting, {.sceneColorOutput = false, .allowInlineRayTracing = true,
		.useConfiguredResolution = true, .worldSpaceHistory = true}) {
}

void Engine::DynamicGIPass::RetainViews(const std::unordered_set<std::string>& activeViews) {

	shared_ = {};
	std::erase_if(states_, [&](const auto& entry) { return !activeViews.contains(entry.first); });
	executor_.RetainViews(activeViews);
}

void Engine::DynamicGIPass::UpdateProbeBricks(const SceneExecutionContext& context, State& state) {

	const uint32_t capacity = state.quality == DynamicGIQuality::High ? 512u : 64u;
	const double spacing = state.quality == DynamicGIQuality::High ? 0.75 : 1.5;
	const double width = spacing * 4.0;
	const auto& scene = context.raytracing;
	brickRegions_.clear();
	for (const auto& geometry : scene.geometryBounds) {
		if (geometry.instanceIndex >= scene.instanceWorldMatrices.size() || geometry.instanceIndex >= scene.instanceData.size() ||
			!((scene.instanceData[geometry.instanceIndex].renderFlags >> 8u) & state.mask)) continue;
		const auto& world = scene.instanceWorldMatrices[geometry.instanceIndex];
		const auto center = Vector3::Transform(geometry.center, world);
		const double position[] = {center.x, center.y, center.z};
		BrickRegion region{};
		uint32_t longest = 1;
		bool valid = true;
		for (int axis = 0; axis < 3; ++axis) {
			// 非一様スケールの床や壁では各軸の広がりを別々に求める
			const double extent = geometry.radius * std::hypot(double(world.m[0][axis]), double(world.m[1][axis]),
				double(world.m[2][axis])) + 2.5 * spacing;
			const double first = std::floor((position[axis] - extent) / width);
			const double last = std::floor((position[axis] + extent) / width);
			if (!std::isfinite(first) || !std::isfinite(last) || last < first ||
				first < -268435456.0 || last > 268435455.0) {
				valid = false;
				break;
			}
			region.first[axis] = static_cast<int32_t>(first);
			region.last[axis] = static_cast<int32_t>(last);
			region.center[axis] = static_cast<int32_t>(std::floor(position[axis] / width));
			longest = std::max(longest, static_cast<uint32_t>(region.last[axis] - region.first[axis] + 1));
		}
		if (!valid) continue;
		region.sizeClass = std::bit_width(longest - 1);
		brickRegions_.push_back(region);
	}
	auto order = [](const BrickRegion& a, const BrickRegion& b) {
		return std::tie(a.sizeClass, a.center, a.first, a.last) < std::tie(b.sizeClass, b.center, b.first, b.last);
	};
	std::sort(brickRegions_.begin(), brickRegions_.end(), order);
	brickRegions_.erase(std::unique(brickRegions_.begin(), brickRegions_.end()), brickRegions_.end());
	if (state.bricks.size() == capacity && brickRegions_ == state.brickRegions) return;
	state.brickRegions.swap(brickRegions_);

	// 近傍の格子が入りきらないシーンでは広域の格子にも一部を割り当てる
	auto collect = [](const std::vector<BrickRegion>& regions, uint32_t limit, int32_t level) {
		std::unordered_set<BrickCoord, BrickHash> unique;
		unique.reserve(limit);
		std::vector<BrickEntry> chosen;
		chosen.reserve(limit);
		auto add = [&](const BrickCoord& cell) {
			if (unique.insert(cell).second) chosen.push_back({cell[0], cell[1], cell[2], level + 1});
		};
		for (size_t begin = 0; begin < regions.size() && chosen.size() < limit;) {
			size_t end = begin + 1;
			while (end < regions.size() && (level != 0 || regions[end].sizeClass == regions[begin].sizeClass)) ++end;
			for (int range = 0; range < static_cast<int>(limit) && chosen.size() < limit; ++range) {
				bool hasShell = false;
				for (size_t index = begin; index < end && chosen.size() < limit; ++index) {
					const auto& region = regions[index];
					const auto& c = region.center;
					const auto& lo = region.first;
					const auto& hi = region.last;
					const int lastShell = std::max(c[0]-lo[0], hi[0]-c[0]) + std::max(c[1]-lo[1], hi[1]-c[1]) +
						std::max(c[2]-lo[2], hi[2]-c[2]);
					if (lastShell < range) continue;
					hasShell = true;
					for (int x = std::max(-range,lo[0]-c[0]); x <= std::min(range,hi[0]-c[0]) && chosen.size() < limit; ++x) {
						const int yz = range - std::abs(x);
						for (int y = std::max(-yz,lo[1]-c[1]); y <= std::min(yz,hi[1]-c[1]) && chosen.size() < limit; ++y) {
							const int z = yz - std::abs(y);
							if (lo[2] <= c[2]-z) add({c[0]+x,c[1]+y,c[2]-z});
							if (z && c[2]+z <= hi[2] && chosen.size() < limit) add({c[0]+x,c[1]+y,c[2]+z});
						}
					}
				}
				if (!hasShell) break;
			}
			begin = end;
		}
		return chosen;
	};
	auto wanted = collect(state.brickRegions, capacity + 1u, 0);
	state.hasCoarseBricks = wanted.size() > capacity;
	if (state.hasCoarseBricks) {
		std::vector<BrickEntry> wide;
		const uint32_t limit = capacity / 4u;
		auto shiftedRegions = [&](uint32_t shift) {
			auto coarse = state.brickRegions;
			for (auto& region : coarse) {
				for (int axis = 0; axis < 3; ++axis) {
					region.first[axis] = (region.first[axis] - 2) >> shift;
					region.last[axis] = (region.last[axis] + 2) >> shift;
					region.center[axis] >>= shift;
				}
				region.sizeClass = 0;
			}
			std::sort(coarse.begin(), coarse.end(), order);
			coarse.erase(std::unique(coarse.begin(), coarse.end()), coarse.end());
			return coarse;
		};
		uint32_t shift = 2u;
		for (;; ++shift) {
			wide = collect(shiftedRegions(shift), limit + 1u, 1);
			if (wide.size() <= limit) break;
		}
		if (state.atmosphere.coarseShift != shift) {
			state.atmosphere.coarseShift = shift;
			state.previousLookup.clear();
		}
		// 室内と街区で格子の間隔が大きく離れる場合は、その中間にも配置する
		std::vector<BrickEntry> middle;
		if (shift >= 4u) {
			const auto limitMiddle = static_cast<uint32_t>((capacity - wide.size()) / 2u);
			middle = collect(shiftedRegions((shift + 1u) / 2u), limitMiddle, 2);
		}
		wanted.resize(capacity - wide.size() - middle.size());
		wanted.insert(wanted.end(), middle.begin(), middle.end());
		wanted.insert(wanted.end(), wide.begin(), wide.end());
	}
	std::sort(wanted.begin(), wanted.end());
	static_assert(sizeof(BrickEntry) == 16);
	state.bricks.resize(capacity);
	std::vector<bool> retained(wanted.size());
	for (auto& slot : state.bricks) {
		auto it = std::lower_bound(wanted.begin(), wanted.end(), slot);
		if (slot[3] && it != wanted.end() && *it == slot) retained[it - wanted.begin()] = true;
		else slot = {};
	}
	size_t next = 0;
	for (auto& slot : state.bricks) {
		if (slot[3]) continue;
		while (next < wanted.size() && retained[next]) ++next;
		if (next == wanted.size()) break;
		slot = wanted[next++];
	}
	state.lookup.assign(2u * capacity, {});
	for (uint32_t slot = 0; slot < capacity; ++slot) {
		const auto& key = state.bricks[slot];
		if (!key[3]) continue;
		const uint32_t mask = 2u * capacity - 1u;
		const uint32_t level = static_cast<uint32_t>(key[3] - 1);
		uint32_t index = (BrickHash{}({key[0],key[1],key[2]}) ^ (level * 0x9e3779b9u)) & mask;
		while (state.lookup[index][3]) index = (index + 1u) & mask;
		const int32_t entry = static_cast<int32_t>(slot + 1u);
		state.lookup[index] = {key[0], key[1], key[2], entry | static_cast<int32_t>(level << 16u)};
	}
	state.previousLookup.resize(state.lookup.size());
	state.layoutPending = state.lookup != state.previousLookup;
}

void Engine::DynamicGIPass::UpdateAtmosphere(const SceneExecutionContext& context, State& state) {

	Entity owner{};
	const auto* component = context.world && context.viewLights ?
		AtmosphereLighting::Find(*context.world, state.mask, owner) : nullptr;
	if (!component) {
		state.atmosphere.lightIndices.fill(UINT32_MAX);
		state.atmosphereRevision = 0;
		return;
	}
	const auto sky = AtmosphereLighting::Sanitize(*component);
	const auto& lights = context.viewLights->directionalLights;
	const std::array bodies{ AtmosphereLighting::ResolveSun(*context.world, owner, sky),
		AtmosphereLighting::ResolveMoon(*context.world, owner, sky) };
	std::array<uint32_t, 2> indices{ UINT32_MAX, UINT32_MAX };
	const float ground = LightExtract::GetWorldPos(*context.world, owner).y;
	uint64_t revision = 1;
	auto add = [&](float value) { Algorithm::HashCombine(revision, std::bit_cast<uint32_t>(value)); };
	for (float value : { ground, sky.planetRadiusKm, sky.atmosphereHeightKm, sky.rayleighScattering.r,
		sky.rayleighScattering.g, sky.rayleighScattering.b, sky.rayleighScaleHeightKm, sky.mieScattering,
		sky.mieAbsorption, sky.mieScaleHeightKm, sky.ozoneAbsorption.r, sky.ozoneAbsorption.g, sky.ozoneAbsorption.b,
		sky.ozoneCenterHeightKm, sky.ozoneWidthKm, sky.mieAnisotropy, sky.groundAlbedo.r, sky.groundAlbedo.g,
		sky.groundAlbedo.b, sky.moonDiskColor.r, sky.moonDiskColor.g, sky.moonDiskColor.b, sky.moonDiskBrightness,
		sky.moonAngularRadius }) add(value);
	Algorithm::HashCombine(revision, sky.moonDiskEnabled);
	for (size_t body = 0; body < bodies.size(); ++body) {
		for (uint32_t i = 0; i < lights.size(); ++i) {
			const auto& light = *lights[i];
			if (light.common.entity != bodies[body]) continue;
			indices[body] = i;
			for (float value : { light.direction.y, light.color.r, light.color.g, light.color.b }) add(value);
			break;
		}
		Algorithm::HashCombine(revision, indices[body]);
	}
	if (revision == state.atmosphereRevision) return;
	state.atmosphereRevision = revision;
	state.atmosphere.lightIndices = indices;
	state.atmosphere.groundHeight = ground;
	state.atmosphere.inverseHeight = 0.001f / sky.atmosphereHeightKm;
	for (size_t body = 0; body < indices.size(); ++body) {
		if (indices[body] == UINT32_MAX) continue;
		const auto& light = *lights[indices[body]];
		auto& samples = state.atmosphere.colors[body];
		for (size_t i = 0; i < samples.size(); ++i) {
			// 地表付近の高度を細かくサンプリングする
			const float t = static_cast<float>(i) / static_cast<float>(samples.size() - 1);
			const auto transmission = AtmosphereLighting::SunTransmittance(sky, sky.atmosphereHeightKm * t * t, -1.0f * light.direction);
			samples[i] = { light.color.r * transmission.x, light.color.g * transmission.y, light.color.b * transmission.z, 0.0f };
		}
	}
}

const Engine::DynamicGIPass::State* Engine::DynamicGIPass::PrepareCacheChanges(GraphicsCore& graphicsCore, SceneExecutionContext& context, State& state) {

	const auto& scene = context.raytracing;
	const uint64_t lightRevision = context.viewLights ? context.viewLights->sourceRevision : 0;
	const uint64_t worldLightRevision = context.world ? context.world->GetLightDataRevision() : 0;
	if (state.bricks.empty() || state.worldLightRevision != worldLightRevision || state.lightRevision != lightRevision) {
		UpdateAtmosphere(context, state);
		state.worldLightRevision = worldLightRevision;
	}
	changedBounds_.clear();
	auto include = [&](const Vector4& bounds) {
		if (bounds.w > 0.0f) changedBounds_.push_back({bounds});
	};
	bool directionalChanged = false;
	if (state.lightRevision != lightRevision || state.bricks.empty()) {
		size_t index = 0;
		uint64_t directional = 0;
		auto stamp = [](const auto& item) {
			const auto data = LightGPUConversion::ToGPU(item);
			return Algorithm::MixHashString(0, {reinterpret_cast<const char*>(&data),sizeof(data)});
		};
		auto local = [&](const auto& light, float radius) {
			const Vector4 sphere{light.pos.x,light.pos.y,light.pos.z,radius};
			const uint64_t hash = stamp(light);
			if (index == state.localLights.size()) {
				state.localLights.emplace_back(hash,sphere);
				include(sphere);
			} else if (state.localLights[index].first != hash || state.localLights[index].second != sphere) {
				include(state.localLights[index].second);
				include(sphere);
				state.localLights[index] = {hash,sphere};
			}
			++index;
		};
		if (const auto* lights = context.viewLights) {
			for (const auto* light : lights->pointLights) local(*light, light->radius);
			for (const auto* light : lights->spotLights) local(*light, light->distance);
			for (const auto* light : lights->rectLights) local(*light, light->attenuationRadius);
			for (const auto* light : lights->directionalLights) Algorithm::HashCombine(directional, stamp(*light));
		}
		for (size_t i = index; i < state.localLights.size(); ++i) include(state.localLights[i].second);
		state.localLights.resize(index);
		directionalChanged = state.directionalRevision != directional;
		state.directionalRevision = directional;
		state.lightRevision = lightRevision;
	}
	const auto& lighting = graphicsCore.GetDXObject().GetFeatureController().GetPreferences();
	state.atmosphere.indirectIntensity = lighting.indirectIntensity;
	state.atmosphere.emissiveContribution = lighting.emissiveContribution;
	uint64_t environment = graphicsCore.GetTextureUploadService().GetContentRevision();
	Algorithm::HashCombine(environment, std::bit_cast<uint32_t>(lighting.emissiveContribution));
	Algorithm::HashCombine(environment, context.lightShadowFilterRevision);
	if (const auto* sky = context.raytracingViewConstants) {
		Algorithm::HashCombine(environment, sky->hasSkybox);
		Algorithm::HashCombine(environment, sky->skyboxCubemapIndex);
		for (float value : {sky->skyboxColor.r, sky->skyboxColor.g, sky->skyboxColor.b, sky->iblIntensity}) {
			Algorithm::HashCombine(environment, std::bit_cast<uint32_t>(value));
		}
	}
	Algorithm::HashCombine(environment, state.atmosphereRevision);
	Algorithm::HashCombine(environment, context.materialPaintRevision);
	const bool globalChange = directionalChanged || state.materialGeneration != scene.materialGeneration ||
		state.environmentRevision != environment;
	if (globalChange) {
		state.environmentRevision = environment;
		state.materialGeneration = scene.materialGeneration;
		state.lightingChangeFrames = 6;
	}
	const uint64_t renderRevision = context.world ? context.world->GetRenderDataRevision() : 0;
	const bool raySceneChanged = state.bricks.empty() || state.geometryGeneration != scene.geometryGeneration;
	const bool geometryChanged = raySceneChanged || state.renderRevision != renderRevision ||
		state.geometry.size() != scene.geometryBounds.size();
	const size_t lightBoundsCount = changedBounds_.size();
	if (geometryChanged) {
		auto includeGeometry = [&](const State::Geometry& geometry) {
			if (geometry.entityIndex == UINT32_MAX) return;
			if (geometry.bounds.sphere.w >= 0.0f) changedBounds_.push_back(geometry.bounds);
			else state.lightingChangeFrames = 6;
		};
		UpdateProbeBricks(context, state);
		state.geometryLayers = 0;
		for (const auto& instance : scene.instanceData) state.geometryLayers |= instance.renderFlags >> 8u;
		const size_t previousCount = state.geometry.size();
		if (previousCount < scene.geometryBounds.size()) state.geometry.resize(scene.geometryBounds.size());
		for (size_t i = 0; i < scene.geometryBounds.size(); ++i) {
			const auto& bounds = scene.geometryBounds[i];
			auto& previous = state.geometry[i];
			if (bounds.instanceIndex >= scene.instanceWorldMatrices.size() || bounds.instanceIndex >= scene.instanceData.size()) {
				includeGeometry(previous);
				state.lightingChangeFrames = 6;
				previous = {};
				continue;
			}
			const auto& world = scene.instanceWorldMatrices[bounds.instanceIndex];
			const auto& instance = scene.instanceData[bounds.instanceIndex];
			GIProbeSurfaceMask::Bounds worldBounds;
			GIProbeSurfaceMask::TryGetWorldBounds(bounds, world, worldBounds);
			const State::Geometry current{worldBounds, world, instance.entityIndex, instance.entityGeneration, instance.renderFlags};
			if (previousCount != 0 && (bounds.deformed || previous.bounds != worldBounds || previous.world != world ||
				previous.entityIndex != instance.entityIndex || previous.entityGeneration != instance.entityGeneration ||
				previous.renderFlags != instance.renderFlags)) {
				includeGeometry(previous);
				includeGeometry(current);
			}
			previous = current;
		}
		for (size_t i = scene.geometryBounds.size(); i < previousCount; ++i) includeGeometry(state.geometry[i]);
		state.geometry.resize(scene.geometryBounds.size());
		state.geometryGeneration = scene.geometryGeneration;
		state.renderRevision = renderRevision;
	}
	if (raySceneChanged || globalChange || !changedBounds_.empty() || scene.usesRenderTextureInputs) {
		if (++state.directRevision == 0) {
			state.directRevision = 1;
			state.clearUpdateFrames = true;
		}
		state.stableDirectFrames = 0;
	} else if (state.stableDirectFrames < 12u) ++state.stableDirectFrames;
	// 外接箱が同じメッシュへの差し替えは範囲を特定できない
	const bool unlocatedGeometryChange = raySceneChanged && changedBounds_.size() == lightBoundsCount;
	if (unlocatedGeometryChange) state.lightingChangeFrames = 6;
	if (!changedBounds_.empty()) {
		state.changedBounds.swap(changedBounds_);
		state.changeFrames = 8;
	}
	if (state.changeFrames != 0) --state.changeFrames;
	else state.changedBounds.clear();
	const uint32_t cycle = ProbeUpdatePeriod(state.quality);
	if (globalChange || unlocatedGeometryChange || state.cacheFrame == 0 || scene.usesRenderTextureInputs)
		state.settlingFrames = cycle * 12u;
	else if (state.settlingFrames) --state.settlingFrames;
	if (constantFrameSerial_ != GraphicsFrameState::GetFrameSerial()) {
		changeConstants_.BeginFrame();
		constantFrameSerial_ = GraphicsFrameState::GetFrameSerial();
	}
	auto& platform = graphicsCore.GetDXObject();
	emitters_.clear();
	for (uint32_t i = 0; i < scene.geometryBounds.size(); ++i) {
		const auto& geometry = scene.geometryBounds[i];
		if (!geometry.emissiveTriangleCount || geometry.instanceIndex >= scene.instanceData.size() ||
			geometry.instanceIndex >= scene.instanceWorldMatrices.size()) continue;
		const auto& instance = scene.instanceData[geometry.instanceIndex];
		if (i < instance.geometryDataOffset || !((instance.renderFlags >> 8u) & state.mask)) continue;
		const auto& world = scene.instanceWorldMatrices[geometry.instanceIndex];
		Vector3 center = geometry.center;
		float radius = geometry.radius;
		MeshBoundsUtility::TransformSphere(world, center, radius);
		emitters_.push_back({world, {center.x, center.y, center.z, radius}, geometry.instanceIndex,
			i - instance.geometryDataOffset, geometry.emissiveTriangleCount, geometry.emitterFaceFlags | (geometry.deformed ? 1u : 0u)});
	}
	BuildEmitterHierarchy();
	const State* other = shared_.frame == constantFrameSerial_ ? shared_.state : nullptr;
	if (!other || other == &state || !shared_.color || !shared_.moments ||
		other->world != state.world || other->sceneID != state.sceneID ||
		((other->mask ^ state.mask) & (other->geometryLayers | state.geometryLayers)) != 0 ||
		other->quality != state.quality || other->materialGeneration != state.materialGeneration ||
		other->geometryGeneration != state.geometryGeneration || other->directionalRevision != state.directionalRevision ||
		other->localLights != state.localLights || other->environmentRevision != state.environmentRevision ||
		shared_.tlas != scene.tlasResource) {
		other = nullptr;
	} else if (other->bricks != state.bricks || other->atmosphere.coarseShift != state.atmosphere.coarseShift) {
		// 途中で開いたViewも、同じワールドなら先に描画したViewのスロット配置を引き継ぐ
		state.bricks = other->bricks;
		state.lookup = other->lookup;
		state.hasCoarseBricks = other->hasCoarseBricks;
		if (state.atmosphere.coarseShift != other->atmosphere.coarseShift) state.previousLookup.assign(state.lookup.size(), {});
		state.atmosphere.coarseShift = other->atmosphere.coarseShift;
		state.layoutPending = state.lookup != state.previousLookup;
	}
	// 別Viewから受け取ったスロットに、手元の一次照明を流用しない
	if (other && state.layoutPending) state.clearUpdateFrames = true;
	struct CacheChanges {
		uint32_t lightingChanged;
		uint32_t emitterCount;
		uint32_t cacheFrame;
		uint32_t cacheConverged;
		uint32_t hasCoarseBricks;
		uint32_t showCacheUpdates;
		uint32_t updateSpeed;
		uint32_t layoutChanged;
		uint32_t allLayersVisible;
		uint32_t copyAllMoments;
		uint32_t directRevision;
	};
	const uint32_t refreshMode = state.settlingFrames ? 0u : (state.changeFrames || state.layoutPending ? 2u : 1u);
	const CacheChanges changes{state.lightingChangeFrames != 0 ? 1u : 0u,
		static_cast<uint32_t>(emitters_.size()), state.cacheFrame, refreshMode,
		state.hasCoarseBricks ? 1u : 0u,
		platform.GetFeatureController().GetPreferences().dynamicGIDebug == DynamicGIDebug::CacheUpdates ? 1u : 0u,
		lighting.giUpdateSpeed, state.layoutPending ? 1u : 0u, (scene.commonLayers & state.mask) != 0 ? 1u : 0u,
		state.syncHistoryFrames || state.layoutPending ? 1u : 0u,
		state.stableDirectFrames >= cycle ? state.directRevision : 0u};
	if (state.lightingChangeFrames != 0) --state.lightingChangeFrames;
	const auto upload = changeConstants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), changes);
	context.bufferRegistry.Register({.alias = "GIChangeBounds", .gpuAddress = upload.gpuAddress});
	const auto atmosphere = changeConstants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), state.atmosphere);
	context.bufferRegistry.Register({.alias = "GIWorldLighting", .gpuAddress = atmosphere.gpuAddress});
	auto uploadBricks = [&](const std::vector<BrickEntry>& entries) {
		const auto* bytes = reinterpret_cast<const uint8_t*>(entries.data());
		return changeConstants_.AllocateAndUploadBytes(platform.GetResourceRetirement(), platform.GetDevice(),
			{bytes, entries.size() * sizeof(BrickEntry)}).gpuAddress;
	};
	state.surfaceMask.Update(context, state.bricks, state.quality == DynamicGIQuality::High ? 0.75f : 1.5f,
		state.atmosphere.coarseShift, state.liveProbeMasks);
	if (other && !other->liveProbeMasks.empty() && other->liveProbeMasks != state.liveProbeMasks) other = nullptr;
	MarkChangedProbes(state);
	if (state.uploadedBricks != probeUpload_) {
		state.uploadedBricks = probeUpload_;
		state.brickBuffer.MarkFullUpdate(static_cast<uint32_t>(probeUpload_.size()));
	}
	state.brickBuffer.UploadCurrentFrame(state.uploadedBricks);
	context.bufferRegistry.Register({.alias = "gGIProbeBricks", .gpuAddress = state.brickBuffer.GetGPUAddress()});
	if (state.layoutPending) state.lookupBuffer.MarkFullUpdate(static_cast<uint32_t>(state.lookup.size()));
	state.lookupBuffer.UploadCurrentFrame(state.lookup);
	const auto lookup = state.lookupBuffer.GetGPUAddress();
	context.bufferRegistry.Register({.alias = "gGIProbeLookup", .gpuAddress = lookup});
	context.bufferRegistry.Register({.alias = "gGIPreviousProbeLookup",
		.gpuAddress = state.layoutPending ? uploadBricks(state.previousLookup) : lookup});
	static_assert(sizeof(Emitter) == 96);
	auto address = upload.gpuAddress;
	if (!emitters_.empty()) {
		const auto* bytes = reinterpret_cast<const uint8_t*>(emitters_.data());
		address = changeConstants_.AllocateAndUploadBytes(platform.GetResourceRetirement(), platform.GetDevice(),
			{bytes, emitters_.size() * sizeof(Emitter)}).gpuAddress;
	}
	context.bufferRegistry.Register({.alias = "gGIEmitters", .gpuAddress = address,
		.elementCount = static_cast<uint32_t>(emitters_.size()), .stride = sizeof(Emitter)});
	return other;
}

void Engine::DynamicGIPass::MarkChangedProbes(const State& state) {

	// 座標表の後ろに各ブリックの64ビット更新マスクを詰める
	const size_t count = state.bricks.size();
	probeUpload_.assign(state.bricks.begin(), state.bricks.end());
	const size_t packedCount = (count + 1) / 2;
	probeUpload_.resize(count + packedCount);
	probeUpload_.resize(count + 2 * packedCount, BrickEntry{-1, -1, -1, -1});
	if (state.liveProbeMasks.size() == count) {
		for (size_t slot = 0; slot < count; ++slot) {
			const uint64_t mask = state.liveProbeMasks[slot];
			auto& packed = probeUpload_[count + packedCount + slot / 2];
			const size_t word = (slot & 1u) * 2u;
			packed[word] = std::bit_cast<int32_t>(uint32_t(mask));
			packed[word + 1] = std::bit_cast<int32_t>(uint32_t(mask >> 32));
		}
	}
	if (state.changedBounds.empty()) return;
	const float baseSpacing = state.quality == DynamicGIQuality::High ? 0.75f : 1.5f;
	for (size_t slot = 0; slot < count; ++slot) {
		const auto& brick = state.bricks[slot];
		if (!brick[3]) continue;
		const uint32_t shift = state.atmosphere.coarseShift;
		const float spacing = std::ldexp(baseSpacing, brick[3] == 3 ? (shift + 1u) / 2u : brick[3] == 2 ? shift : 0u);
		const Vector3 first{(4.0f * brick[0] + 0.5f) * spacing, (4.0f * brick[1] + 0.5f) * spacing,
			(4.0f * brick[2] + 0.5f) * spacing};
		uint64_t mask = 0;
		for (const auto& bounds : state.changedBounds) {
			mask |= bounds.GetProbeMask(first, spacing, spacing, mask);
			if (mask == UINT64_MAX) break;
		}
		auto& packed = probeUpload_[count + slot / 2];
		const size_t word = (slot & 1u) * 2u;
		packed[word] = std::bit_cast<int32_t>(uint32_t(mask));
		packed[word + 1] = std::bit_cast<int32_t>(uint32_t(mask >> 32));
	}
}

void Engine::DynamicGIPass::BuildEmitterHierarchy() {

	if (emitters_.size() <= 8) return;
	if (emitterLeaves_.size() == emitters_.size() &&
		!std::memcmp(emitterLeaves_.data(), emitters_.data(), emitters_.size() * sizeof(Emitter))) {
		emitters_ = emitterTree_;
		return;
	}
	emitterLeaves_ = emitters_;
	emitterTree_.clear();
	auto append = [&](auto&& self, size_t first, size_t last) -> void {
		if (last - first <= 4) {
			emitterTree_.insert(emitterTree_.end(), emitters_.begin() + first, emitters_.begin() + last);
			return;
		}
		const auto index = emitterTree_.size();
		emitterTree_.emplace_back();
		std::array<float, 3> lo{FLT_MAX, FLT_MAX, FLT_MAX}, hi{-FLT_MAX, -FLT_MAX, -FLT_MAX};
		bool deformed = false;
		for (size_t i = first; i < last; ++i) {
			const auto& b = emitters_[i].bounds;
			const std::array coords{b.x, b.y, b.z};
			for (size_t a = 0; a < 3; ++a) {
				lo[a] = std::min(lo[a], coords[a] - b.w);
				hi[a] = std::max(hi[a], coords[a] + b.w);
			}
			deformed |= (emitters_[i].flags & 1u) != 0;
		}
		const std::array extent{hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]};
		const auto axis = std::max_element(extent.begin(), extent.end()) - extent.begin();
		const size_t mid = first + (last - first) / 2;
		std::nth_element(emitters_.begin() + first, emitters_.begin() + mid, emitters_.begin() + last,
			[axis](const Emitter& a, const Emitter& b) {
				auto key = [axis](const Emitter& e) { return axis == 0 ? e.bounds.x : axis == 1 ? e.bounds.y : e.bounds.z; };
				return std::tuple{key(a), a.instance, a.geometry} < std::tuple{key(b), b.instance, b.geometry};
			});
		self(self, first, mid);
		self(self, mid, last);
		auto& node = emitterTree_[index];
		node.bounds = {(lo[0]+hi[0])*0.5f, (lo[1]+hi[1])*0.5f, (lo[2]+hi[2])*0.5f,
			std::hypot(extent[0], extent[1], extent[2])*0.5f};
		node.world.m[0][0] = extent[0] * 0.5f;
		node.world.m[1][1] = extent[1] * 0.5f;
		node.world.m[2][2] = extent[2] * 0.5f;
		node.triangleCount = 0;
		node.instance = static_cast<uint32_t>(emitterTree_.size());
		node.flags = deformed ? 1u : 0u;
	};
	append(append, 0, emitters_.size());
	emitters_ = emitterTree_;
}

bool Engine::DynamicGIPass::PrepareUpdateQueue(GraphicsCore& graphicsCore, SceneExecutionContext& context, uint32_t capacity) {

	auto& platform = graphicsCore.GetDXObject();
	auto& commands = *platform.GetDxCommand();
	if (!dispatchSignature_) {
		D3D12_INDIRECT_ARGUMENT_DESC arg{};
		arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
		D3D12_COMMAND_SIGNATURE_DESC desc{};
		desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
		desc.NumArgumentDescs = 1;
		desc.pArgumentDescs = &arg;
		const HRESULT result = platform.GetDevice()->CreateCommandSignature(&desc, nullptr, IID_PPV_ARGS(&dispatchSignature_));
		if (!DxDREDDiagnostics::CheckHRESULT(platform.GetDevice(), result, "DynamicGI::CreateCommandSignature")) return false;
		updateJobs_.Init(platform.GetDevice(), &graphicsCore.GetSRVDescriptor());
		updateArguments_.Init(platform.GetDevice(), &graphicsCore.GetSRVDescriptor());
	}
	updateJobs_.EnsureCapacity(capacity);
	updateArguments_.EnsureCapacity(3u);
	auto upload = changeConstants_.Reserve(platform.GetResourceRetirement(), platform.GetDevice(), sizeof(D3D12_DISPATCH_ARGUMENTS));
	if (!upload.resource || !upload.mapped) return false;
	const D3D12_DISPATCH_ARGUMENTS arguments{0u, 6u, 1u};
	std::memcpy(upload.mapped, &arguments, sizeof(arguments));
	updateArguments_.Transition(commands, D3D12_RESOURCE_STATE_COPY_DEST);
	commands.GetCommandList()->CopyBufferRegion(updateArguments_.GetResource(), 0u, upload.resource, upload.offset, sizeof(arguments));
	updateArguments_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	updateJobs_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.bufferRegistry.Register({.alias = "gGIUpdateJobs", .gpuAddress = updateJobs_.GetGPUAddress(), .uavGPUHandle = updateJobs_.GetUAVGPUHandle()});
	context.bufferRegistry.Register({.alias = "gGIUpdateArguments", .gpuAddress = updateArguments_.GetGPUAddress(), .uavGPUHandle = updateArguments_.GetUAVGPUHandle()});
	context.bufferRegistry.Register({.alias = "gGISelectedJobs", .gpuAddress = updateJobs_.GetGPUAddress(), .srvGPUHandle = updateJobs_.GetSRVGPUHandle()});
	return true;
}

void Engine::DynamicGIPass::Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	context.diffuseGI = nullptr;
	context.diffuseGICache = nullptr;
	context.diffuseGIMoments = nullptr;
	context.diffuseGIDebug = 0;
	const auto& platform = graphicsCore.GetDXObject();
	if (!platform.ShouldUseDynamicGI()) {
		if (!states_.empty()) {
			shared_ = {};
			states_.clear();
			executor_.RetainViews({});
		}
		return;
	}
	if (!context.view || !context.targetRegistry || !context.resources || context.disableInlineRayTracing) return;
	if (!context.raytracing.tlasResource || !context.raytracing.materialTexturesReady) {
		const auto previous = states_.find(context.view->GetHistoryKey());
		const UUID sceneID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
		if (previous != states_.end() && (!context.raytracing.tlasResource || previous->second.world != context.world ||
			previous->second.sceneID != sceneID)) previous->second.surfaceMask.Release();
		return;
	}
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	const auto* target = context.resources->GetSceneFinal();
	if (!camera || !target || !target->GetWidth() || !target->GetHeight()) return;

	const auto& preferences = platform.GetFeatureController().GetPreferences();
	const auto width = target->GetWidth();
	const auto height = target->GetHeight();
	const uint32_t mask = context.view->GetCullingMask(RenderCameraDomain::Perspective) & 0x00ffffffu;
	auto& state = states_[context.view->GetHistoryKey()];
	const UUID sceneID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	const bool resetHistory = !state.generation || state.quality != preferences.dynamicGIQuality ||
		state.mask != mask || state.world != context.world || state.sceneID != sceneID;
	if (resetHistory || state.width != width || state.height != height) {
		state.world = context.world;
		state.sceneID = sceneID;
		state.runtime.Rebuild(BuildProfile(preferences.dynamicGIQuality, mask, width, height));
		state.quality = preferences.dynamicGIQuality;
		state.width = width;
		state.height = height;
		state.mask = mask;
		if (resetHistory) {
			if (!state.generation) {
				auto* srv = &graphicsCore.GetSRVDescriptor();
				auto* uploads = &graphicsCore.GetBufferUploadService();
				state.brickBuffer.Init(platform.GetDevice(), srv, uploads);
				state.lookupBuffer.Init(platform.GetDevice(), srv, uploads);
				state.updateFrames.Init(platform.GetDevice(), srv);
				state.directLighting.Init(platform.GetDevice(), srv);
			}
			state.generation = ++generation_;
			state.clearUpdateFrames = true;
			state.stableDirectFrames = 0;
			state.bricks.clear();
			state.previousLookup.clear();
			state.geometry.clear();
			state.changedBounds = {};
			state.cacheFrame = 0;
			state.syncHistoryFrames = 2;
		}
	}
	const ScopedValue runtime(context.renderPassesRuntime, &state.runtime);
	const ScopedValue generation(context.renderPassesGeneration, state.generation);
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	if (HasExpiredGraphicsResource(state.lastCacheSerial, serial)) {
		state.cacheFrame = 0;
		state.syncHistoryFrames = 2;
	}
	const State* other = PrepareCacheChanges(graphicsCore, context, state);
	const ScopedValue materialGeneration(context.raytracing.materialGeneration, uint64_t{0});
	const uint32_t n = state.quality == DynamicGIQuality::High ? 32u : 16u;
	if (!PrepareUpdateQueue(graphicsCore, context, n * n * n)) return;
	const uint32_t probeCount = n * n * n;
	state.updateFrames.EnsureCapacity(probeCount);
	if (state.clearUpdateFrames) {
		const size_t bytes = sizeof(std::array<uint32_t, 2>) * probeCount;
		const auto zeros = changeConstants_.Reserve(graphicsCore.GetDXObject().GetResourceRetirement(), platform.GetDevice(), bytes);
		if (!zeros.mapped) return;
		std::memset(zeros.mapped, 0, bytes);
		state.updateFrames.Transition(*platform.GetDxCommand(), D3D12_RESOURCE_STATE_COPY_DEST);
		platform.GetDxCommand()->GetCommandList()->CopyBufferRegion(state.updateFrames.GetResource(), 0, zeros.resource, zeros.offset, bytes);
		state.clearUpdateFrames = false;
	}
	state.directLighting.EnsureCapacity(state.stableDirectFrames >= ProbeUpdatePeriod(state.quality) ? probeCount * 6u : 1u);
	state.directLighting.Transition(*platform.GetDxCommand(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.bufferRegistry.Register({.alias = "gGICachedLighting", .gpuAddress = state.directLighting.GetGPUAddress(),
		.uavGPUHandle = state.directLighting.GetUAVGPUHandle()});
	state.updateFrames.Transition(*platform.GetDxCommand(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	context.bufferRegistry.Register({.alias = "gGIProbeUpdateFrames", .gpuAddress = state.updateFrames.GetGPUAddress(),
		.uavGPUHandle = state.updateFrames.GetUAVGPUHandle()});
	const RenderFeatureComputeWorkQueue queue{kCachePass, BuiltinAssets::Materials::GICacheSelect,
		updateJobs_, updateArguments_, dispatchSignature_.Get(), n / 8u, n * n / 8u};
	if (other) {
		const RenderFeatureSharedOutput outputs[] = {{"gDestColor", shared_.color}, {"gDestMoments", shared_.moments}};
		executor_.ExecuteWithSharedOutputs(graphicsCore, passBuckets, context, kCachePass, outputs, &queue);
	} else executor_.ExecuteWithSharedOutputs(graphicsCore, passBuckets, context, {}, {}, &queue);
	state.updateFrames.Transition(*platform.GetDxCommand(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	state.directLighting.Transition(*platform.GetDxCommand(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	if (executor_.HasExecutionSucceeded()) {
		state.lastCacheSerial = serial;
		if (other || state.layoutPending) state.syncHistoryFrames = 2;
		else if (state.syncHistoryFrames) --state.syncHistoryFrames;
		if (state.layoutPending) {
			state.previousLookup = state.lookup;
			state.layoutPending = false;
		}
		++state.cacheFrame;
		if (other) {
			state.cacheFrame = other->cacheFrame;
			state.settlingFrames = other->settlingFrames;
		} else {
			shared_ = {&state, context.raytracing.tlasResource, serial,
				context.targetRegistry->Find(RenderFeatureResourceUtility::MakeOutputAlias({kCachePass, "Color"})),
				context.targetRegistry->Find(RenderFeatureResourceUtility::MakeOutputAlias({kCachePass, "Moments"}))};
		}
		const auto alias = RenderFeatureResourceUtility::MakeOutputAlias({kResolvePass, "Color"});
		context.diffuseGI = context.targetRegistry->FindColorByName(alias);
		context.diffuseGICache = context.targetRegistry->FindColorByName(RenderFeatureResourceUtility::MakeOutputAlias({kCachePass, "Color"}));
		context.diffuseGIMoments = context.targetRegistry->FindColorByName(RenderFeatureResourceUtility::MakeOutputAlias({kCachePass, "Moments"}));
		context.diffuseGIDebug = static_cast<uint32_t>(preferences.dynamicGIDebug);
	} else {
		// 後段で失敗した場合も格子の配置と書き込み済み履歴をそろえて破棄する
		state.generation = 0;
	}
}
