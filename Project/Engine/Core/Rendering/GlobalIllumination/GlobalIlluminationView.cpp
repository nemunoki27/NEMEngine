#include "GlobalIlluminationView.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <algorithm>
#include <cmath>
#include <cfloat>

//============================================================================
//	GlobalIlluminationView classMethods
//============================================================================
uint32_t Engine::GlobalIlluminationView::GetUpdateCount(float budgetMilliseconds, const ResolvedRenderView& view,
	uint32_t quality) const {

	// 遅れて確定した計測値だけを更新量へ反映
	float costPerProbe = 0.025f * static_cast<float>(1u << quality);
	float geometryCost = 0.0f;
	const std::string key = view.GetHistoryKey();
	for (const auto& pass : FrameProfiler::GetInstance().GetGPUPassTimes()) {

		if (pass.viewID != key) continue;
		if (pass.name == "GI/ProbeUpdate" && pass.milliseconds > 0.0f) {

			const auto& record = updateRecords_[pass.frameID % updateRecords_.size()];
			if (record.frameID == pass.frameID && record.probeCount > 0u) {

				costPerProbe = pass.milliseconds / static_cast<float>(record.probeCount);
			}
		}
		if (pass.name == "GI/Geometry") geometryCost = pass.milliseconds;
	}
	const float available = std::max(budgetMilliseconds - geometryCost, 0.0f);
	return static_cast<uint32_t>(std::clamp(available / std::max(costPerProbe, 0.001f), 1.0f, 128.0f));
}

void Engine::GlobalIlluminationView::UpdateGrid(const Vector3& cameraPosition,
	const GlobalIlluminationSettings& settings, uint32_t updateCount) {

	// 原点をProbe間隔へ揃えて移動中も履歴を保持
	for (uint32_t level = 0; level < constants_.levelCount; ++level) {

		const float spacing = settings.probeSpacing * static_cast<float>(1u << (level * 2u));
		const auto align = [&](float value) { return (std::floor(value / spacing) - 4.0f) * spacing; };
		auto& origin = constants_.gridOrigins[level];
		// 格子中央の余裕がなくなるまで原点を動かさない
		const auto retain = [&](float current, float camera) {
			const float local = (camera - current) / spacing;
			return ready_ && origin.w == spacing && local >= 2.0f && local <= 5.0f ? current : align(camera);
		};
		origin = Vector4(retain(origin.x, cameraPosition.x), retain(origin.y, cameraPosition.y),
			retain(origin.z, cameraPosition.z), spacing);
	}
	constants_.startProbe = nextProbe_;
	// 再配置されたProbeは通常の巡回より先に更新
	float nearest = FLT_MAX;
	for (uint32_t index = 0; index < kProbeCount; ++index) {

		const Vector3 position = GetNominalProbePosition(index);
		if (publishedPositionsValid_[index] && (position - publishedPositions_[index]).Length() < settings.probeSpacing * 0.01f) continue;
		const float spacing = constants_.gridOrigins[index / 512u].w;
		const float priority = (position - cameraPosition).Length() / spacing;
		if (priority < nearest) {
			nearest = priority;
			constants_.startProbe = index;
		}
	}
	constants_.updateCount = std::min(updateCount, 128u);
	constants_.rayCount = 64u << settings.quality;
	constants_.frameIndex = static_cast<uint32_t>(GraphicsFrameState::GetFrameSerial());
	constants_.maxRayDistance = settings.maxRayDistance;
	constants_.normalBias = settings.probeSpacing * 0.05f;
	constants_.viewBias = 0.0f;
	constants_.debugMode = settings.debugMode;
}

void Engine::GlobalIlluminationView::Update(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	RenderAssetLibrary& assetLibrary, MaterialResolver& materialResolver, MeshRenderBackend* meshBackend,
	PrimitiveGeometryManager* primitiveGeometryManager, const RenderSceneBatch& batch, uint32_t updateCount) {

	const auto& controller = graphicsCore.GetDXObject().GetFeatureController();
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!controller.GetRuntimeFeatures().useGlobalIllumination || !camera || !camera->useGlobalIllumination) {
		if (initialized_) Release();
		return;
	}
	const uint64_t frame = GraphicsFrameState::GetFrameSerial();
	if (frameSerial_ == frame) {
		if (ready_) context.globalIllumination = this;
		return;
	}
	frameSerial_ = frame;
	if (!EnsureResources(graphicsCore)) return;
	if (graphRevision_ && graphRevision_ != assetLibrary.GetMaterialRevision()) {

		// Graph編集後はGeometryとCallableを同じ世代へ更新
		sceneBuilder_.Finalize();
		geometry_.Clear();
		materials_.Clear();
		tracePipeline_.reset();
		ClearFields(graphicsCore);
	}
	graphRevision_ = assetLibrary.GetMaterialRevision();
	const auto& settings = controller.GetPreferences().globalIllumination;
	const uint32_t cullingMask = context.view->GetCullingMask(RenderCameraDomain::Perspective);
	const bool reset = settings_.probeSpacing != settings.probeSpacing || settings_.maxRayDistance != settings.maxRayDistance ||
		settings_.quality != settings.quality || cullingMask_ != cullingMask;
	if (reset) {

		ClearFields(graphicsCore);
		sceneBuilder_.Finalize();
	}
	settings_ = settings;
	cullingMask_ = cullingMask;
	constants_.resetHistory = ready_ ? 0u : 1u;
	UpdateGrid(camera->cameraPos, settings, updateCount);
	if (constants_.updateCount == 0u) {
		if (ready_) context.globalIllumination = this;
		return;
	}

	// GI用Sceneは通常の影と反射の所有を変更しない
	SceneExecutionContext giContext = context;
	giContext.raytracing = {};
	if (geometryCacheSource_) sceneBuilder_.ShareGeometryCache(*geometryCacheSource_);
	const auto& origin = constants_.gridOrigins.back();
	const Vector3 center(origin.x + origin.w * 3.5f, origin.y + origin.w * 3.5f, origin.z + origin.w * 3.5f);
	sceneBuilder_.SetGlobalIlluminationRange(center, origin.w * 7.0f + settings.maxRayDistance * 2.0f);
	geometry_.BeginFrame();
	sceneBuilder_.BeginFrame(graphicsCore);
	auto* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	auto& profiler = GPUFrameProfiler::GetInstance();
	profiler.BeginPass(commandList, "GI/Geometry");
	sceneBuilder_.BuildForScene(graphicsCore, *context.assetDatabase, assetLibrary, materialResolver,
		meshBackend, primitiveGeometryManager, batch, giContext);
	profiler.EndPass(commandList);
	if (!giContext.raytracing.tlasResource || !EnsureTracePipeline(graphicsCore)) return;
	constants_.hysteresis = materialGeneration_ == giContext.raytracing.materialGeneration ? 0.9f : 0.2f;
	materialGeneration_ = giContext.raytracing.materialGeneration;
	constantAllocator_.BeginFrame();
	constantsAddress_ = constantAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
		graphicsCore.GetDXObject().GetDevice(), constants_).gpuAddress;

	const uint32_t destination = publishedField_ ^ 1u;
	auto& command = *graphicsCore.GetDXObject().GetDxCommand();
	const uint64_t profilingFrame = FrameProfiler::GetInstance().GetFrameID();
	updateRecords_[profilingFrame % updateRecords_.size()] = { profilingFrame, constants_.updateCount };
	profiler.BeginPass(commandList, "GI/ProbeUpdate");
	CopyField(graphicsCore, destination);
	command.SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	rayResults_.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	commandList->SetComputeRootSignature(tracePipeline_->GetRootSignature());
	commandList->SetPipelineState1(tracePipeline_->GetStateObject());
	if (!BindCompute(graphicsCore, giContext, tracePipeline_->GetReflection(),
		[&](std::string_view name, ShaderBindingKind kind) { return tracePipeline_->FindBindingByName(name, kind); }, destination)) {
		profiler.EndPass(commandList);
		return;
	}
	const auto dispatch = tracePipeline_->BuildDispatchDesc(constants_.rayCount, constants_.updateCount);
	commandList->DispatchRays(&dispatch);
	command.UAVBarrier(rayResults_.GetResource());
	rayResults_.Transition(command, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	// 命中結果を照度と距離へ蓄積
	commandList->SetComputeRootSignature(blendPipeline_->GetRootSignature());
	commandList->SetPipelineState(blendPipeline_->GetComputePipeline());
	if (!BindCompute(graphicsCore, giContext, blendPipeline_->GetComputeReflection(),
		[&](std::string_view name, ShaderBindingKind kind) { return blendPipeline_->FindBindingByName(name, kind); }, destination)) {
		profiler.EndPass(commandList);
		return;
	}
	commandList->Dispatch(constants_.updateCount, 1, 1);
	auto& field = fields_[destination];
	for (auto* texture : { &field.irradiance, &field.distance, &field.positions, &field.offsets }) {

		command.UAVBarrier(texture->GetResource());
		texture->Transition(command, static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
	profiler.EndPass(commandList);
	// 全入力の接続とGPU投入が成功してから公開
	publishedField_ = destination;
	// GPU投入に成功した位置だけ履歴として保持
	for (uint32_t offset = 0; offset < constants_.updateCount; ++offset) {

		const uint32_t index = (constants_.startProbe + offset) % kProbeCount;
		publishedPositions_[index] = GetNominalProbePosition(index);
		publishedPositionsValid_[index] = true;
	}
	nextProbe_ = (constants_.startProbe + constants_.updateCount) % kProbeCount;
	ready_ = true;
	context.globalIllumination = this;
}

void Engine::GlobalIlluminationView::ShareGeometryCache(const RaytracingSceneBuilder& source) {

	geometryCacheSource_ = &source;
	sceneBuilder_.ShareGeometryCache(source);
}

Engine::Vector3 Engine::GlobalIlluminationView::GetNominalProbePosition(uint32_t index) const {

	const auto& origin = constants_.gridOrigins[index / 512u];
	const uint32_t slot = index % 512u;
	const auto axis = [&](uint32_t ring, float first) {
		const int32_t cell = static_cast<int32_t>(std::round(first / origin.w));
		const int32_t local = ((static_cast<int32_t>(ring) - cell) % 8 + 8) % 8;
		return static_cast<float>(cell + local) * origin.w;
	};
	return Vector3(axis(slot % 8u, origin.x), axis((slot / 8u) % 8u, origin.y), axis(slot / 64u, origin.z));
}
