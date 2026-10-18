#include "DecalPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Decals/DecalCollector.h>
#include <Engine/Core/Rendering/Decals/DecalProjection.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>

// c++
#include <span>

namespace {

	// 配置とMaterialと候補の変更を同じ世代へまとめる
	template<typename T>
	void HashValues(uint64_t& hash, const std::vector<T>& values) {

		for (const std::byte byte : std::as_bytes(std::span<const T>(values))) {
			hash = (hash ^ std::to_integer<uint8_t>(byte)) * 1099511628211ull;
		}
	}
}

void Engine::DecalPass::Prepare(GraphicsCore& graphicsCore, SceneExecutionContext& context, State& state) {

	DecalCollector::Collect(graphicsCore, *deps_.assetLibrary, context, state.items);
	const bool raytracingEnabled = !context.disableInlineRayTracing &&
		graphicsCore.GetDXObject().GetFeatureController().ShouldUseDispatchRays() &&
		context.postProcessCamera.surfaceSettings.reflectionEnabled;
	if (state.items.empty() && !raytracingEnabled) return;
	state.gpuData.clear();
	for (const auto& item : state.items) state.gpuData.push_back(item.gpu);
	const uint32_t decalCount = static_cast<uint32_t>(state.gpuData.size());
	const auto& raytracing = context.raytracing;
	state.ranges.assign(raytracingEnabled ? raytracing.geometryBounds.size() : 0, {});
	state.indices.clear();
	// 反射先のGeometryと交差する投影だけをShaderへ渡す
	for (size_t index = 0; index < state.ranges.size() && decalCount > 0; ++index) {
		const auto& bounds = raytracing.geometryBounds[index];
		if (!bounds.receiveDecals || bounds.instanceIndex >= raytracing.instanceWorldMatrices.size() ||
			bounds.instanceIndex >= raytracing.instanceData.size()) continue;
		const auto& matrix = raytracing.instanceWorldMatrices[bounds.instanceIndex];
		Vector3 center = bounds.center;
		float radius = bounds.radius;
		MeshBoundsUtility::TransformSphere(matrix, center, radius);
		const uint32_t mask = raytracing.instanceData[bounds.instanceIndex].renderFlags >> 8u;
		auto& range = state.ranges[index];
		range.offset = static_cast<uint32_t>(state.indices.size());
		for (uint32_t decal = 0; decal < decalCount; ++decal) {
			const auto& data = state.gpuData[decal];
			if ((mask & data.receiverMask) && DecalProjectionUtility::Intersects(data.worldToDecal, center, radius)) {
				state.indices.push_back(decal);
				++range.count;
			}
		}
	}
	uint64_t hash = 14695981039346656037ull;
	HashValues(hash, state.gpuData);
	HashValues(hash, state.ranges);
	HashValues(hash, state.indices);
	if (hash == 0) hash = 1;
	// 小さな空Bufferも正しい範囲数と組み合わせて公開する
	const uint32_t receiverCount = static_cast<uint32_t>(state.ranges.size());
	if (state.gpuData.empty()) state.gpuData.emplace_back().channels = 0;
	if (state.ranges.empty()) state.ranges.emplace_back();
	if (state.indices.empty()) state.indices.push_back(0);
	if (!state.initialized) {
		auto* device = graphicsCore.GetDXObject().GetDevice();
		auto* descriptors = &graphicsCore.GetSRVDescriptor();
		state.decals.Init(device, descriptors);
		state.receivers.Init(device, descriptors);
		state.candidates.Init(device, descriptors);
		state.initialized = true;
	}
	// 同frameの先行描画を残して待機済みslotへ転送する
	state.decals.Upload(state.gpuData);
	state.receivers.Upload(state.ranges);
	state.candidates.Upload(state.indices);
	const auto publish = [&](const char* name, const auto& buffer, uint32_t count, uint32_t stride) {
		context.bufferRegistry.Register({.alias = name, .resource = buffer.GetResource(), .gpuAddress = buffer.GetGPUAddress(),
			.srvGPUHandle = buffer.GetGPUHandle(), .elementCount = count, .stride = stride});
	};
	publish("gDecals", state.decals, decalCount, sizeof(DecalGPUData));
	publish("gDecalReceiverRanges", state.receivers, receiverCount, sizeof(DecalReceiverRange));
	publish("gDecalReceiverIndices", state.candidates, static_cast<uint32_t>(state.indices.size()), sizeof(uint32_t));
	if (constantFrameSerial_ != GraphicsFrameState::GetFrameSerial()) {
		constants_.BeginFrame();
		constantFrameSerial_ = GraphicsFrameState::GetFrameSerial();
	}
	const uint32_t counts[]{decalCount, receiverCount, 0, 0};
	const auto allocation = constants_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
		graphicsCore.GetDXObject().GetDevice(), counts);
	context.bufferRegistry.Register({.alias = "DecalSceneConstants", .gpuAddress = allocation.gpuAddress});
	// 削除や設定変更を反射の蓄積履歴へ伝える
	context.raytracing.materialGeneration ^= hash + 0x9e3779b97f4a7c15ull +
		(context.raytracing.materialGeneration << 6) + (context.raytracing.materialGeneration >> 2);
}
