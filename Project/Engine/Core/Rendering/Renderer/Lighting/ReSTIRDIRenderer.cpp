#include "ReSTIRDIRenderer.h"
#include "ShadowSceneStamp.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <array>
#include <chrono>
#include <map>
#include <tuple>
#include <unordered_map>

namespace {

	using namespace Engine;
	constexpr auto kShaderRead = static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	struct ReSTIRParameters {
		uint32_t width, height, frameIndex, resetHistory;
		Vector3 cameraPos;
		uint32_t remapLights;
		Matrix4x4 viewMatrix;
		float shadowNormalBias, shadowMaxDistance;
		uint32_t shadowSamples, historyLimit;
	};

	using LightKey = std::tuple<uint32_t, uint32_t, uint32_t>;
	using Reservoir = std::array<uint32_t, 12>;

	struct ViewState {
		StructuredRWBuffer<Reservoir> candidates, history;
		std::array<StructuredRWBuffer<std::array<uint32_t, 2>>, 3> radiance;
		uint32_t filtered = 1, samplingFrame = 0;
		uint32_t stableFrames = 0, shadowSamples = 0;
		bool usedExact = false, exactHistoryReady = false;
		ShadowSceneStamp stamp;
		uint64_t lightRevision = 0, geometryGeneration = 0, filterRevision = 0, materialGeneration = 0;
		StructuredInstanceBuffer<uint32_t> remap;
		std::vector<LightKey> previousLights, currentLights;
		std::weak_ptr<const ECSWorldLifetime> world;
		Entity camera = Entity::Null();
		Vector3 position{}, forward{};
		Matrix4x4 projection{};
		uint64_t lastFrame = UINT64_MAX;
		std::chrono::steady_clock::time_point lastRender{};
		uint32_t width = 0, height = 0;
	};

	bool UploadLightRemap(ViewState& state, const PerViewLightSet& lights) {
		auto& current = state.currentLights;
		current.clear();
		current.reserve(lights.GetLocalLightCount());
		const auto append = [&](const auto& group, uint32_t type) {
			for (const auto* item : group) {
				const auto entity = item->common.entity;
				current.emplace_back(type, entity.index, entity.generation);
			}
		};
		append(lights.pointLights, 0);
		append(lights.spotLights, 1);
		append(lights.rectLights, 2);
		const bool reordered = current != state.previousLights;
		if (!reordered) {
			if (state.remap.GetResource()) state.remap.RetainCurrentForFrame();
			else state.remap.EnsureCapacity(1);
			return false;
		}
		std::vector<uint32_t> remap((std::max)(size_t{1}, state.previousLights.size()), UINT32_MAX);
		std::map<LightKey, uint32_t> indices;
		for (uint32_t i = 0; i < current.size(); ++i) indices.emplace(current[i], i);
		for (size_t i = 0; i < state.previousLights.size(); ++i) {
			if (const auto it = indices.find(state.previousLights[i]); it != indices.end()) remap[i] = it->second;
		}
		state.remap.Upload(remap);
		state.previousLights.swap(current);
		return reordered;
	}
}

struct Engine::ReSTIRDIRenderer::Implementation {

	std::array<std::unique_ptr<PipelineState>, 4> pipelines;
	std::array<RegistryAutoBindTable, 4> registryBindings;
	std::unordered_map<std::string, std::unique_ptr<ViewState>> views;
	FrameConstantBufferAllocator constants;
	uint64_t constantFrame = UINT64_MAX;

	bool EnsurePipelines(GraphicsCore& graphics, bool exact, bool stable) {
		auto& platform = graphics.GetDXObject();
		for (size_t i = 0; i < pipelines.size(); ++i) {
			if (pipelines[i] || (exact && i != (stable ? 3u : 2u))) continue;
			ComputePipelineDesc desc{};
			desc.compute.file = "Builtin/Lighting/restirDI.CS.hlsl";
			constexpr AssetID shaders[]{BuiltinAssets::Shaders::ReSTIRInitial, BuiltinAssets::Shaders::ReSTIRSpatial,
				BuiltinAssets::Shaders::ReSTIRShade, BuiltinAssets::Shaders::ReSTIRDenoise};
			constexpr const char* entries[]{"initialTemporal", "spatial", "shade", "denoise"};
			desc.compute.shader = shaders[i];
			desc.compute.entry = entries[i];
			desc.compute.profile = "cs_6_6";
			pipelines[i] = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
				platform.GetDxShaderCompiler(), desc);
			if (!pipelines[i]) return false;
		}
		return true;
	}
};

Engine::ReSTIRDIRenderer::ReSTIRDIRenderer() : implementation_(std::make_unique<Implementation>()) {}
Engine::ReSTIRDIRenderer::~ReSTIRDIRenderer() = default;

void Engine::ReSTIRDIRenderer::RetainViews(const std::unordered_set<std::string>& viewKeys) {
	std::erase_if(implementation_->views, [&](const auto& entry) { return !viewKeys.contains(entry.first); });
}

bool Engine::ReSTIRDIRenderer::Render(GraphicsCore& graphics, SceneExecutionContext& context, const RenderPipelineDeps& deps) {

	auto& impl = *implementation_;
	auto& platform = graphics.GetDXObject();
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	const auto* lights = context.viewLights;
	const auto* tlas = context.bufferRegistry.Find("gSceneTLAS");
	if (!platform.GetFeatureController().GetRuntimeFeatures().useReSTIRDI || context.disableInlineRayTracing ||
		!tlas || !tlas->gpuAddress || !camera || !camera->valid || !lights || lights->GetLocalLightCount() < 2) {
		if (!impl.views.empty()) impl.views.erase(context.view->GetHistoryKey());
		return false;
	}
	const bool exact = lights->GetLocalLightCount() <= 64 || context.maxWorldCellLights <= 64;
	auto* gbuffer = context.resources->GetSceneMain();
	const uint32_t width = gbuffer->GetWidth(), height = gbuffer->GetHeight();
	const uint64_t pixels = uint64_t(width) * height;
	if (!pixels || UINT32_MAX < pixels) return false;
	auto& owner = impl.views[context.view->GetHistoryKey()];
	if (!owner || owner->width != width || owner->height != height) {
		auto next = std::make_unique<ViewState>();
		next->width = width;
		next->height = height;
		auto* device = platform.GetDevice();
		auto* srv = &graphics.GetSRVDescriptor();
		next->remap.Init(device, srv);
		for (auto* buffer : {&next->history, &next->candidates}) {
			buffer->Init(device, srv);
		}
		for (auto& buffer : next->radiance) {
			buffer.Init(device, srv);
		}
		owner = std::move(next);
	}

	auto& state = *owner;
	const uint32_t historyPixels = exact ? 1u : static_cast<uint32_t>(pixels);
	state.history.EnsureCapacity(historyPixels);
	state.candidates.EnsureCapacity(historyPixels);
	state.radiance[0].EnsureCapacity(static_cast<uint32_t>(pixels));
	state.radiance[1].EnsureCapacity(historyPixels);
	state.radiance[2].EnsureCapacity(historyPixels);
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	const auto now = std::chrono::steady_clock::now();
	bool reset = state.usedExact != exact || state.lastFrame == UINT64_MAX || serial <= state.lastFrame ||
		now - state.lastRender > std::chrono::milliseconds(250) ||
		state.world.lock() != lights->sourceWorldLifetime || state.camera != camera->sourceCamera ||
		state.projection != camera->matrices.unjitteredProjectionMatrix ||
		Vector3::Dot(camera->forward, state.forward) < 0.8f;
	const bool lightSetChanged = !exact && UploadLightRemap(state, *lights);
	if (impl.constantFrame != serial) {
		impl.constants.BeginFrame();
		impl.constantFrame = serial;
	}
	const bool lightingChanged = state.lightRevision != lights->sourceRevision ||
		state.geometryGeneration != context.raytracing.geometryGeneration || state.filterRevision != context.lightShadowFilterRevision ||
		state.materialGeneration != context.raytracing.materialGeneration;
	// 移動する遮蔽物は再投影先で検査し、光源の入れ替えだけ履歴を切る
	const auto samples = context.hasShadowCastingLight ? platform.GetFeatureController().GetRuntimeFeatures().softShadowSampleCount : 0u;
	constexpr uint32_t settleFrames = 64;
	const bool wasStable = state.stableFrames == settleFrames;
	const bool cameraMoved = state.position != camera->cameraPos || state.forward != camera->forward;
	const bool stableInputs = !reset && !cameraMoved && !lightSetChanged && !lightingChanged && samples == state.shadowSamples;
	auto stamp = exact && !stableInputs ? ShadowSceneStamp{} : BuildShadowSceneStamp(graphics, context, deps);
	const bool unchanged = stableInputs && stamp.reusable && stamp == state.stamp;
	state.stableFrames = unchanged ? (std::min)(settleFrames, state.stableFrames + 1) : 0;
	state.stamp = std::move(stamp);
	state.shadowSamples = samples;
	const bool stable = state.stableFrames == settleFrames;
	const bool exactStatic = exact && stable;
	if (!impl.EnsurePipelines(graphics, exact, stable)) return false;
	// 静止が続いた時だけ、受光面ごとの再利用判定に必要な履歴を確保する
	if (exactStatic) state.candidates.EnsureCapacity(static_cast<uint32_t>(pixels));
	reset |= wasStable && !stable;
	uint32_t historyLimit = lightSetChanged ? 1u : ((lightingChanged || cameraMoved) ? 12u : 32u);
	if (exactStatic) historyLimit = state.exactHistoryReady ? UINT32_MAX - 3u : UINT32_MAX - 2u;
	else if (exact) historyLimit = UINT32_MAX - 1u;
	else if (stable) historyLimit = UINT32_MAX;
	const ReSTIRParameters params{width, height, state.samplingFrame++, reset ? 1u : 0u,
		camera->cameraPos, lightSetChanged ? 1u : 0u, camera->matrices.viewMatrix, 0.05f, 100000.0f,
		samples, historyLimit};
	const auto cb = impl.constants.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), params);
	auto& cmd = *platform.GetDxCommand();
	auto* list = cmd.GetCommandList();
	cmd.SetDescriptorHeaps({graphics.GetSRVDescriptor().GetDescriptorHeap()});
	constexpr std::array<uint32_t, 6> attachments{0, 1, 2, 3, 5, 6};
	constexpr std::array<const char*, 6> names{"gAlbedo", "gNormal", "gWorldPos", "gMaterial", "gFlags", "gMotion"};
	for (auto index : attachments) gbuffer->GetColorTexture(index)->Transition(cmd, kShaderRead);
	auto& profiler = GPUFrameProfiler::GetInstance();
	const std::string prefix = std::string(EnumAdapter<RenderViewKind>::ToStringView(context.kind)) + "/Lighting ReSTIR ";
	const uint32_t writeIndex = 3 - state.filtered;
	constexpr const char* labels[]{"Candidates Temporal", "Spatial", "Visibility", "Denoise"};
	const size_t end = exact && !stable ? 3u : impl.pipelines.size();
	for (size_t stage = stable ? 3u : exact ? 2u : 0u; stage < end; ++stage) {
		profiler.BeginPass(list, prefix + (stable ? "Static" : exact ? "Exact" : labels[stage]));
		auto& input = stage == 1 ? state.candidates : state.history;
		auto& output = stage == 0 || stage == 3 ? state.candidates : state.history;
		input.Transition(cmd, kShaderRead);
		auto& radianceOut = state.radiance[exact || stage == 2 ? 0 : writeIndex];
		if (stage < 2) output.Transition(cmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		else radianceOut.Transition(cmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		if (stage == 3) output.Transition(cmd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		auto& rawInput = state.radiance[exactStatic ? state.filtered : 0u];
		if (stage == 3) {
			rawInput.Transition(cmd, kShaderRead);
			state.radiance[state.filtered].Transition(cmd, kShaderRead);
		}
		auto& pipe = *impl.pipelines[stage];
		list->SetComputeRootSignature(pipe.GetRootSignature());
		list->SetPipelineState(pipe.GetComputePipeline());
		pipe.BindGlobalDescriptorTablesCompute(list, graphics.GetSRVDescriptor().GetGPUHandle(0));
		auto& table = impl.registryBindings[stage];
		table.Sync(pipe, context.bufferRegistry);
		table.BindCompute(context.bufferRegistry, list);
		std::array<ComputeBindItem, 14> bindings{{
			{"ReSTIRParameters", ComputeBindValueType::CBV, cb.gpuAddress},
			{"gReSTIRInput", ComputeBindValueType::SRV, input.GetGPUAddress(), input.GetSRVGPUHandle()},
			{"gReSTIROutput", ComputeBindValueType::UAV, output.GetGPUAddress(), output.GetUAVGPUHandle()},
			{"gReSTIRLightRemap", ComputeBindValueType::SRV, state.remap.GetGPUAddress(), state.remap.GetGPUHandle()},
			{"gSceneTLAS", ComputeBindValueType::AccelStruct, tlas->gpuAddress, tlas->srvGPUHandle},
			{"gReSTIRRaw", ComputeBindValueType::SRV, rawInput.GetGPUAddress(), rawInput.GetSRVGPUHandle()},
			{"gReSTIRPrevious", ComputeBindValueType::SRV, state.radiance[state.filtered].GetGPUAddress(), state.radiance[state.filtered].GetSRVGPUHandle()},
			{"gReSTIRRadianceOut", ComputeBindValueType::UAV, radianceOut.GetGPUAddress(), radianceOut.GetUAVGPUHandle()}
		}};
		for (size_t i = 0; i < attachments.size(); ++i) {
			bindings[8 + i] = {names[i], ComputeBindValueType::SRV, 0, gbuffer->GetColorTexture(attachments[i])->GetSRVGPUHandle()};
		}
		ComputeRootBinder{pipe}.Bind(list, bindings);
		list->Dispatch((width + 7) / 8, (height + 7) / 8, 1);
		profiler.EndPass(list);
	}
	if (exactStatic) state.candidates.Transition(cmd, kShaderRead);
	state.exactHistoryReady = exactStatic;
	if (!exact) state.filtered = writeIndex;
	state.usedExact = exact;
	auto& result = state.radiance[exact ? 0u : writeIndex];
	result.Transition(cmd, kShaderRead);
	context.bufferRegistry.Register({"gReSTIRRadiance", result.GetResource(), result.GetGPUAddress(),
		result.GetSRVGPUHandle(), {}, static_cast<uint32_t>(pixels), sizeof(uint32_t) * 2});
	state.lightRevision = lights->sourceRevision;
	state.geometryGeneration = context.raytracing.geometryGeneration;
	state.filterRevision = context.lightShadowFilterRevision;
	state.materialGeneration = context.raytracing.materialGeneration;
	state.lastFrame = serial;
	state.lastRender = now;
	state.world = lights->sourceWorldLifetime;
	state.position = camera->cameraPos;
	state.forward = camera->forward;
	state.camera = camera->sourceCamera;
	state.projection = camera->matrices.unjitteredProjectionMatrix;
	return true;
}
