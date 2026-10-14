#include "InlineShadowRenderer.h"
#include "ShadowSceneStamp.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Lighting/GPU/LightGPUConversion.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace {

	using namespace Engine;

	constexpr auto kShaderRead = static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	struct ShadowParameters {

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t lightCount = 0;
		uint32_t frameIndex = 0;
		uint32_t sampleCount = 4;
		uint32_t resetHistory = 1;
		uint32_t filterStep = 1;
		uint32_t previewLightIndex = 0;
		float normalBias = 0.05f;
		float maxDistance = 100000.0f;
		float projectionScale = 1.0f;
		float orthographic = 0.0f;
		Matrix4x4 view = Matrix4x4::Identity();
		Matrix4x4 previousViewProjection = Matrix4x4::Identity();
	};

	struct ShadowLightSelection {

		std::vector<uint32_t> sources;
		std::vector<uint32_t> indices;
		std::vector<std::byte> signature;
	};

	ShadowLightSelection SelectShadowLights(const PerViewLightSet& lights) {

		ShadowLightSelection result;
		result.indices.resize(lights.GetTotalCount(), UINT32_MAX);
		uint32_t index = 0;
		const auto append = [&](const auto& items) {

			for (const auto* item : items) {
				if (item->shadowStrength > 0.0f && item->intensity > 0.0f) {

					result.indices[index] = static_cast<uint32_t>(result.sources.size());
					result.sources.push_back(index);
					const auto light = LightGPUConversion::ToGPU(*item);
					const auto bytes = std::as_bytes(std::span{ &light, 1 });
					result.signature.insert(result.signature.end(), bytes.begin(), bytes.end());
					const auto entity = std::as_bytes(std::span{ &item->common.entity, 1 });
					result.signature.insert(result.signature.end(), entity.begin(), entity.end());
				}
				++index;
			}
		};
		// GPUのライト配列と同じ順序で対応表を作る
		append(lights.directionalLights);
		append(lights.pointLights);
		append(lights.spotLights);
		append(lights.rectLights);
		return result;
	}

	void RegisterBuffer(RenderBufferRegistry& registry, const char* name,
		ID3D12Resource* resource, D3D12_GPU_DESCRIPTOR_HANDLE handle, uint32_t count, uint32_t stride) {

		registry.Register({ name, resource, resource->GetGPUVirtualAddress(), handle, {}, count, stride });
	}
}

struct Engine::InlineShadowRenderer::Implementation {

	struct ViewState {

		std::array<StructuredRWBuffer<Vector2>, 5> buffers;
		std::array<MultiRenderTarget, 2> geometry;
		MultiRenderTarget preview;
		StructuredInstanceBuffer<uint32_t> sources;
		StructuredInstanceBuffer<uint32_t> indices;
		ShadowLightSelection selection;
		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t lightCount = 0;
		uint32_t historyIndex = 0;
		uint64_t lastFrame = UINT64_MAX;
		std::chrono::steady_clock::time_point lastRenderTime{};
		uint64_t materialGeneration = 0;
		ShadowSceneStamp sceneStamp;
		uint32_t stableFrames = 0;
		uint32_t sampleCount = 0;
		uint32_t filteredBuffer = 0;
		Matrix4x4 previousViewProjection = Matrix4x4::Identity();
		Matrix4x4 projection = Matrix4x4::Identity();
	};

	std::array<std::unique_ptr<PipelineState>, 3> pipelines;
	std::unordered_map<std::string, std::unique_ptr<ViewState>> views;
	FrameConstantBufferAllocator constants;
	RegistryAutoBindTable autoBind;
	std::vector<ComputeBindItem> bindingScratch;
	uint64_t constantFrame = UINT64_MAX;

	// 追跡と時間・空間フィルターのShaderを揃える
	bool EnsurePipelines(GraphicsCore& graphicsCore);
	// 完成した描画資源だけをViewへ渡す
	std::unique_ptr<ViewState> CreateView(GraphicsCore& graphicsCore, uint32_t width, uint32_t height, uint32_t lightCount);
	// 1工程の入力と出力を接続して実行する
	void Dispatch(GraphicsCore& graphicsCore, SceneExecutionContext& context, ViewState& state,
		uint32_t stage, uint32_t input, uint32_t output, const ShadowParameters& parameters);
	// 構築済みの影を現在のLightingへ接続する
	void Publish(SceneExecutionContext& context, ViewState& state);
};

bool Engine::InlineShadowRenderer::Implementation::EnsurePipelines(GraphicsCore& graphicsCore) {

	auto& platform = graphicsCore.GetDXObject();
	const std::array<AssetID, 3> shaders = { BuiltinAssets::Shaders::InlineShadowTrace,
		BuiltinAssets::Shaders::InlineShadowTemporal, BuiltinAssets::Shaders::InlineShadowSpatial };
	const std::array<const char*, 3> entries = { "main", "temporal", "spatial" };
	for (uint32_t stage = 0; stage < pipelines.size(); ++stage) {

		if (pipelines[stage]) continue;
		ComputePipelineDesc desc{};
		desc.compute.file = stage == 0 ? "Builtin/Lighting/inlineShadowTrace.CS.hlsl" : "Builtin/Lighting/inlineShadowFilter.CS.hlsl";
		desc.compute.shader = shaders[stage];
		desc.compute.entry = entries[stage];
		desc.compute.profile = stage == 0 ? "cs_6_6" : "cs_6_0";
		pipelines[stage] = PipelineStateBuilder::CreateCompute(
			platform.GetResourceRetirement(), platform.GetDevice(), platform.GetDxShaderCompiler(), desc);
		if (!pipelines[stage]) return false;
	}
	return true;
}

std::unique_ptr<Engine::InlineShadowRenderer::Implementation::ViewState>
Engine::InlineShadowRenderer::Implementation::CreateView(
	GraphicsCore& graphicsCore, uint32_t width, uint32_t height, uint32_t lightCount) {

	auto state = std::make_unique<ViewState>();
	state->width = width;
	state->height = height;
	state->lightCount = lightCount;
	const uint64_t count = uint64_t(width) * height * lightCount;
	if (count > UINT32_MAX) throw std::length_error("レイトレーシング影の画素数が多すぎます");
	auto* device = graphicsCore.GetDXObject().GetDevice();
	auto* descriptor = &graphicsCore.GetSRVDescriptor();

	// 遮蔽率と履歴をライトの数だけ確保
	for (auto& buffer : state->buffers) {
		buffer.Init(device, descriptor);
		buffer.EnsureCapacity(static_cast<uint32_t>(count));
	}
	state->sources.Init(device, descriptor);
	state->indices.Init(device, descriptor);
	MultiRenderTargetCreateDesc desc{};
	desc.width = width;
	desc.height = height;
	desc.colors = { { "ShadowPosition", DXGI_FORMAT_R32G32B32A32_FLOAT, Color4::Black(), true },
		{ "ShadowNormal", DXGI_FORMAT_R16G16B16A16_FLOAT, Color4::Black(), true } };
	for (auto& geometry : state->geometry) {
		geometry.Create(device, &graphicsCore.GetRTVDescriptor(), &graphicsCore.GetDSVDescriptor(), descriptor, desc);
	}
	// 平滑化前後を同じViewから比較できるようにする
	desc.colors = { { "InlineShadowRaw", DXGI_FORMAT_R16_FLOAT, Color4::White(), true },
		{ "InlineShadowFiltered", DXGI_FORMAT_R16_FLOAT, Color4::White(), true } };
	state->preview.Create(device, &graphicsCore.GetRTVDescriptor(), &graphicsCore.GetDSVDescriptor(), descriptor, desc);
	return state;
}

void Engine::InlineShadowRenderer::Implementation::Dispatch(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	ViewState& state, uint32_t stage, uint32_t input, uint32_t output, const ShadowParameters& parameters) {

	auto& platform = graphicsCore.GetDXObject();
	auto& command = *platform.GetDxCommand();
	auto* list = command.GetCommandList();
	auto& pipeline = *pipelines[stage];
	auto* gbuffer = context.resources->GetSceneMain();
	state.buffers[output].Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	if (stage != 0) state.buffers[input].Transition(command, kShaderRead);
	list->SetComputeRootSignature(pipeline.GetRootSignature());
	list->SetPipelineState(pipeline.GetComputePipeline());
	pipeline.BindGlobalDescriptorTablesCompute(list, graphicsCore.GetSRVDescriptor().GetGPUHandle(0));
	if (stage == 0) {
		autoBind.Sync(pipeline, context.bufferRegistry);
		autoBind.BindCompute(context.bufferRegistry, list);
	}
	const auto allocation = constants.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), parameters);
	// 連続する影処理でバインド配列を再利用する
	auto& bindings = bindingScratch;
	bindings.clear();
	bindings.reserve(16);
	bindings.push_back({ "InlineShadowParameters", ComputeBindValueType::CBV, allocation.gpuAddress });
	bindings.push_back({ "gShadowOutput", ComputeBindValueType::UAV,
		state.buffers[output].GetGPUAddress(), state.buffers[output].GetUAVGPUHandle() });
	bindings.push_back({ "gShadowLightSources", ComputeBindValueType::SRV,
		state.sources.GetGPUAddress(), state.sources.GetGPUHandle() });
	const auto bindTexture = [&](const char* name, RenderTexture2D* texture, bool writable = false) {
		bindings.push_back({ name, writable ? ComputeBindValueType::UAV : ComputeBindValueType::SRV,
			0, writable ? texture->GetUAVGPUHandle() : texture->GetSRVGPUHandle() });
	};
	bindTexture("gNormal", gbuffer->GetColorTexture(1));
	bindTexture("gWorldPos", gbuffer->GetColorTexture(2));
	bindTexture("gFlags", gbuffer->GetColorTexture(5));
	bindTexture("gMotion", gbuffer->GetColorTexture(6));
	if (stage == 0) {

		const auto& tlas = *context.bufferRegistry.Find("gSceneTLAS");
		bindings.push_back({ "gSceneTLAS", ComputeBindValueType::AccelStruct, tlas.gpuAddress, tlas.srvGPUHandle });
		bindTexture("gShadowPreview", state.preview.GetColorTexture(0), true);
	} else {

		const uint32_t current = state.historyIndex;
		const uint32_t previous = 1u - current;
		bindings.push_back({ "gShadowInput", ComputeBindValueType::SRV,
			state.buffers[input].GetGPUAddress(), state.buffers[input].GetSRVGPUHandle() });
		bindings.push_back({ "gShadowRaw", ComputeBindValueType::SRV,
			state.buffers[0].GetGPUAddress(), state.buffers[0].GetSRVGPUHandle() });
		bindings.push_back({ "gShadowHistory", ComputeBindValueType::SRV, state.buffers[1 + previous].GetGPUAddress(),
			state.buffers[1 + previous].GetSRVGPUHandle() });
		bindTexture("gPreviousPosition", state.geometry[previous].GetColorTexture(0));
		bindTexture("gPreviousNormal", state.geometry[previous].GetColorTexture(1));
		bindTexture("gCurrentPosition", state.geometry[current].GetColorTexture(0), true);
		bindTexture("gCurrentNormal", state.geometry[current].GetColorTexture(1), true);
		bindTexture("gSurfaceNormal", state.geometry[current].GetColorTexture(1));
		bindTexture("gShadowPreview", state.preview.GetColorTexture(1), true);
	}
	// Shaderで使う入力だけを名前で接続
	ComputeRootBinder{ pipeline }.Bind(list, bindings);
	list->Dispatch((state.width + 7) / 8, (state.height + 7) / 8, state.lightCount);
	command.UAVBarrier(state.buffers[output].GetResource());
	state.buffers[output].Transition(command, kShaderRead);
	if (stage == 1) {
		command.UAVBarrier(state.geometry[state.historyIndex].GetColorTexture(0)->GetResource());
		// 面の法線を後段の空間フィルターへ渡す
		auto* normal = state.geometry[state.historyIndex].GetColorTexture(1);
		command.UAVBarrier(normal->GetResource());
		normal->Transition(command, kShaderRead);
	} else {
		command.UAVBarrier(state.preview.GetColorTexture(stage == 0 ? 0 : 1)->GetResource());
	}
}

//============================================================================
//	InlineShadowRenderer classMethods
//============================================================================

Engine::InlineShadowRenderer::InlineShadowRenderer() : implementation_(std::make_unique<Implementation>()) {}

Engine::InlineShadowRenderer::~InlineShadowRenderer() = default;

void Engine::InlineShadowRenderer::RetainViews(const std::unordered_set<std::string>& viewKeys) {

	// World切替後の履歴もGPU完了まで回収窓口で保持
	std::erase_if(implementation_->views, [&](const auto& entry) { return !viewKeys.contains(entry.first); });
}

bool Engine::InlineShadowRenderer::Render(GraphicsCore& graphicsCore, SceneExecutionContext& context,
	const RenderPipelineDeps& deps) {

	const auto& features = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	const auto* tlas = context.bufferRegistry.Find("gSceneTLAS");
	if (!features.useInlineRayTracing || context.disableInlineRayTracing || !context.viewLights || !camera ||
		!tlas || tlas->gpuAddress == 0 || !context.hasShadowCastingLight) return false;
	auto selection = SelectShadowLights(*context.viewLights);
	if (selection.sources.empty() || !implementation_->EnsurePipelines(graphicsCore)) return false;
	auto& owner = implementation_->views[context.view->GetHistoryKey()];
	const uint32_t width = context.resources->GetSceneMain()->GetWidth();
	const uint32_t height = context.resources->GetSceneMain()->GetHeight();
	const uint32_t lightCount = static_cast<uint32_t>(selection.sources.size());
	if (!owner || owner->width != width || owner->height != height || owner->lightCount != lightCount) {

		// リサイズ前の資源を使用中のGPUへ残す
		owner = implementation_->CreateView(graphicsCore, width, height, lightCount);
	}
	auto& state = *owner;
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	auto stamp = BuildShadowSceneStamp(graphicsCore, context, deps);
	const bool cachedGeometryChanged = state.stableFrames >= 64 &&
		stamp.geometryGeneration != state.sceneStamp.geometryGeneration;
	const bool samplingChanged = state.sampleCount != features.softShadowSampleCount;
	// 副Viewの更新間隔では履歴を失効させない
	const auto renderTime = std::chrono::steady_clock::now();
	const bool historyInterrupted = state.lastFrame == UINT64_MAX || serial < state.lastFrame ||
		renderTime - state.lastRenderTime > std::chrono::milliseconds(250);
	const bool inputsUnchanged = stamp.reusable && stamp == state.sceneStamp && !historyInterrupted &&
		state.selection.signature == selection.signature && state.selection.indices == selection.indices &&
		state.sampleCount == features.softShadowSampleCount && state.materialGeneration == context.raytracing.materialGeneration;
	state.stableFrames = inputsUnchanged ? (std::min)(state.stableFrames + 1, 64u) : 0u;
	state.sceneStamp = std::move(stamp);
	state.sampleCount = features.softShadowSampleCount;
	if (state.stableFrames >= 64) {

		// 十分に積算した静止画の影を再利用する
		auto& profiler = GPUFrameProfiler::GetInstance();
		auto* list = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
		const std::string prefix = std::string(EnumAdapter<RenderViewKind>::ToStringView(context.kind)) + "/Lighting Shadow ";
		for (const char* stage : { "Trace", "Temporal", "Spatial" }) {

			// 再利用中も計測項目の並びを保つ
			profiler.BeginPass(list, prefix + stage);
			profiler.EndPass(list);
		}
		implementation_->Publish(context, state);
		state.lastFrame = serial;
		state.lastRenderTime = renderTime;
		return true;
	}
	if (implementation_->constantFrame != serial) {
		implementation_->constants.BeginFrame();
		implementation_->constantFrame = serial;
	}
	ShadowParameters parameters;
	parameters.width = width;
	parameters.height = height;
	parameters.lightCount = lightCount;
	parameters.frameIndex = static_cast<uint32_t>(serial);
	parameters.sampleCount = features.softShadowSampleCount;
	parameters.previewLightIndex = lightCount - 1;
	parameters.resetHistory = cachedGeometryChanged || samplingChanged || historyInterrupted ||
		state.selection.signature != selection.signature ||
		state.selection.indices != selection.indices || state.materialGeneration != context.raytracing.materialGeneration ||
		state.projection != camera->matrices.projectionMatrix;
	parameters.view = camera->matrices.viewMatrix;
	parameters.previousViewProjection = state.previousViewProjection;
	parameters.projectionScale = std::max(std::abs(camera->matrices.projectionMatrix.m[1][1]) * height * 0.5f, 0.001f);
	parameters.orthographic = camera->projectionMode == ResolvedProjectionMode::Orthographic ? 1.0f : 0.0f;
	state.selection = std::move(selection);
	state.sources.Upload(state.selection.sources);
	state.indices.Upload(state.selection.indices);
	state.historyIndex = 1u - state.historyIndex;
	auto& command = *graphicsCore.GetDXObject().GetDxCommand();
	command.SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	context.resources->GetSceneMain()->TransitionForShaderRead(command);
	for (uint32_t attachment = 0; attachment < 2; ++attachment) {

		state.geometry[state.historyIndex].GetColorTexture(attachment)->Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		state.geometry[1 - state.historyIndex].GetColorTexture(attachment)->Transition(command, kShaderRead);
		state.preview.GetColorTexture(attachment)->Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
	state.buffers[2 - state.historyIndex].Transition(command, kShaderRead);

	// 追跡結果を時間積算してから段階的に平滑化
	auto& profiler = GPUFrameProfiler::GetInstance();
	auto* list = command.GetCommandList();
	const std::string prefix = std::string(EnumAdapter<RenderViewKind>::ToStringView(context.kind)) + "/Lighting Shadow ";
	profiler.BeginPass(list, prefix + "Trace");
	implementation_->Dispatch(graphicsCore, context, state, 0, 0, 0, parameters);
	profiler.EndPass(list);
	profiler.BeginPass(list, prefix + "Temporal");
	implementation_->Dispatch(graphicsCore, context, state, 1, 0, 1 + state.historyIndex, parameters);
	profiler.EndPass(list);
	profiler.BeginPass(list, prefix + "Spatial");
	uint32_t input = 1 + state.historyIndex;
	for (uint32_t iteration = 0; iteration < 4; ++iteration) {

		parameters.filterStep = 1u << iteration;
		const uint32_t output = 3 + (iteration & 1u);
		implementation_->Dispatch(graphicsCore, context, state, 2, input, output, parameters);
		input = output;
	}
	profiler.EndPass(list);
	state.preview.TransitionForShaderRead(command);
	state.filteredBuffer = input;
	implementation_->Publish(context, state);
	state.lastFrame = serial;
	state.lastRenderTime = renderTime;
	state.previousViewProjection = camera->matrices.viewProjectionMatrix;
	state.projection = camera->matrices.projectionMatrix;
	state.materialGeneration = context.raytracing.materialGeneration;
	return true;
}

void Engine::InlineShadowRenderer::Implementation::Publish(SceneExecutionContext& context, ViewState& state) {

	// 最後に完成したBufferと確認用の画像を公開する
	auto& filtered = state.buffers[state.filteredBuffer];
	RegisterBuffer(context.bufferRegistry, "gFilteredRaytracedShadows", filtered.GetResource(),
		filtered.GetSRVGPUHandle(), state.width * state.height * state.lightCount, sizeof(Vector2));
	RegisterBuffer(context.bufferRegistry, "gShadowLightIndices", state.indices.GetResource(),
		state.indices.GetGPUHandle(), static_cast<uint32_t>(state.selection.indices.size()), sizeof(uint32_t));
	if (context.targetRegistry) {
		context.targetRegistry->Register("InlineShadows", &state.preview, { "InlineShadowRaw", "InlineShadowFiltered" }, {});
	}
}
