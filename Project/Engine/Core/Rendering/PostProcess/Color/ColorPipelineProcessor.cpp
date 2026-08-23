#include "ColorPipelineProcessor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

//============================================================================
//	ColorPipelineProcessor classMethods
//============================================================================
namespace {

	Engine::Vector3 CalculateTemperatureRGB(float temperature) {

		const float value = std::clamp(temperature, 1000.0f, 15000.0f) / 100.0f;
		float red = 255.0f;
		float green = 255.0f;
		float blue = 255.0f;
		if (66.0f < value) {
			red = 329.698727446f * std::pow(value - 60.0f, -0.1332047592f);
		}
		if (value <= 66.0f) {
			green = 99.4708025861f * std::log(value) - 161.1195681661f;
		} else {
			green = 288.1221695283f * std::pow(value - 60.0f, -0.0755148492f);
		}
		if (66.0f <= value) {
			blue = 255.0f;
		} else if (value <= 19.0f) {
			blue = 0.0f;
		} else {
			blue = 138.5177312231f * std::log(value - 10.0f) - 305.0447927307f;
		}
		return Engine::Vector3(
			std::clamp(red, 0.0f, 255.0f) / 255.0f,
			std::clamp(green, 0.0f, 255.0f) / 255.0f,
			std::clamp(blue, 0.0f, 255.0f) / 255.0f);
	}

	bool BindColorTargetsOnly(Engine::GraphicsCore& graphicsCore,
		Engine::MultiRenderTarget& target) {

		if (target.GetColorCount() == 0) {
			return false;
		}

		std::vector<Engine::RenderTarget> renderTargets{};
		renderTargets.reserve(target.GetColorCount());
		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		for (uint32_t index = 0; index < target.GetColorCount(); ++index) {

			Engine::RenderTexture2D* color = target.GetColorTexture(index);
			if (!color) {
				return false;
			}
			color->Transition(*dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
			renderTargets.emplace_back(color->GetRenderTarget());
		}
		dxCommand->BindRenderTargets(renderTargets, std::nullopt);
		dxCommand->SetViewportAndScissor(target.GetWidth(), target.GetHeight());
		return true;
	}
}

Engine::ColorPipelineProcessor::ColorPipelineProcessor() {

	exposureConstantsSlot_ = exposureBindingCache_.AddSlotByRegister(
		ShaderBindingKind::CBV, 0, 0);
	exposureSourceSlot_ = exposureBindingCache_.AddSlotByRegister(
		ShaderBindingKind::SRV, 0, 0);
	exposureOutputSlot_ = exposureBindingCache_.AddSlotByRegister(
		ShaderBindingKind::UAV, 0, 0);
	toneMapConstantsSlot_ = toneMapBindingCache_.AddSlotByRegister(
		ShaderBindingKind::CBV, 0, 0);
	toneMapSourceSlot_ = toneMapBindingCache_.AddSlotByRegister(
		ShaderBindingKind::SRV, 0, 0);
	toneMapExposureSlot_ = toneMapBindingCache_.AddSlotByRegister(
		ShaderBindingKind::SRV, 1, 0);
	outputTransformConstantsSlot_ = outputTransformBindingCache_.AddSlotByRegister(
		ShaderBindingKind::CBV, 0, 0);
	outputTransformSourceSlot_ = outputTransformBindingCache_.AddSlotByRegister(
		ShaderBindingKind::SRV, 0, 0);
}

void Engine::ColorPipelineProcessor::BeginFrame() {

	constantBufferAllocator_.BeginFrame();
}

void Engine::ColorPipelineProcessor::Release() {

	for (ViewExposureState& state : viewStates_) {
		state.exposureBuffer.Release();
		state.world = nullptr;
		state.lastUpdatedFrame = 0;
		state.initialized = false;
		state.bufferInitialized = false;
	}
	outputTransformLogged_ = false;
	constantBufferAllocator_.Release();
}

bool Engine::ColorPipelineProcessor::ToneMap(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, MultiRenderTarget* source,
	MultiRenderTarget* dest, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, const ColorPipelineSettings& settings,
	bool updateExposure) {

	if (!source || !dest || !source->GetColorTexture(0) ||
		!dest->GetColorTexture(0)) {
		return false;
	}

	ViewExposureState& state = GetViewState(context.kind);
	if (!state.bufferInitialized) {
		state.exposureBuffer.Init(graphicsCore.GetDXObject().GetDevice(),
			&graphicsCore.GetSRVDescriptor());
		state.exposureBuffer.EnsureCapacity(1);
		state.bufferInitialized = true;
	}

	const bool sceneChanged = state.world != context.world;
	if (sceneChanged) {
		state.world = context.world;
		state.initialized = false;
	}
	const bool resetExposure = !state.initialized;
	const ColorPipelineConstants constants = BuildConstants(
		graphicsCore, context, *source, settings, resetExposure);
	const PostProcessConstantBufferAllocation allocation =
		constantBufferAllocator_.AllocateAndUpload(
			graphicsCore.GetDXObject().GetDevice(), constants);
	if (!allocation.gpuAddress) {
		return false;
	}

	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if ((updateExposure && state.lastUpdatedFrame != frameSerial) || resetExposure) {

		if (!UpdateExposure(graphicsCore, context, *source,
			assetLibrary, pipelineCache, state, constants, allocation.gpuAddress)) {
			return false;
		}
		state.lastUpdatedFrame = frameSerial;
		state.initialized = true;
	}

	const bool drawn = DrawToneMap(graphicsCore, *source, *dest,
		assetLibrary, pipelineCache, state, allocation.gpuAddress);
	if (drawn) {
		dest->TransitionForShaderRead(
			*graphicsCore.GetDXObject().GetDxCommand());
	}
	return drawn;
}

bool Engine::ColorPipelineProcessor::PresentToBackBuffer(
	GraphicsCore& graphicsCore, MultiRenderTarget* source,
	RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache) {

	if (!source || !source->GetColorTexture(0)) {
		return false;
	}
	const MaterialAsset* material = assetLibrary.LoadMaterial(
		BuiltinAssets::Materials::OutputTransform);
	if (!material) {
		return false;
	}
	const MaterialPassBinding* pass = FindPass(*material,
		MaterialPassKind::Blit);
	if (!pass) {
		pass = FindPass(*material, MaterialPassKind::Fullscreen);
	}
	if (!pass || pass->preferredVariant == PipelineVariantKind::Compute ||
		pass->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	const DXGI_FORMAT backBufferFormat =
		graphicsCore.GetBackBufferRenderTarget().format;
	const PipelineState* pipeline = pipelineCache.GetORCreate(
		graphicsCore.GetDXObject(), assetLibrary, pass->pipeline,
		pass->preferredVariant,
		std::span<const DXGI_FORMAT>(&backBufferFormat, 1),
		DXGI_FORMAT_UNKNOWN);
	if (!pipeline || !pipeline->GetGraphicsPipeline(BlendMode::Normal)) {
		return false;
	}

	const DisplayOutputSettings& output =
		graphicsCore.GetDisplayOutputSettings();
	OutputTransformConstants constants{};
	constants.outputMode = static_cast<uint32_t>(output.mode);
	constants.paperWhiteNits = output.paperWhiteNits;
	constants.maxLuminanceNits = output.maxLuminanceNits;
	const PostProcessConstantBufferAllocation allocation =
		constantBufferAllocator_.AllocateAndUpload(
			graphicsCore.GetDXObject().GetDevice(), constants);
	if (!allocation.gpuAddress) {
		return false;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();
	source->TransitionForShaderRead(*dxCommand);
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetGraphicsRootSignature(pipeline->GetRootSignature());
	commandList->SetPipelineState(
		pipeline->GetGraphicsPipeline(BlendMode::Normal));

	outputTransformBindingCache_.Sync(*pipeline);
	if (!outputTransformBindingCache_.Has(outputTransformConstantsSlot_) ||
		!outputTransformBindingCache_.Has(outputTransformSourceSlot_)) {
		return false;
	}
	RootBindingCommand::SetGraphicsCBV(commandList,
		outputTransformBindingCache_.Get(outputTransformConstantsSlot_),
		allocation.gpuAddress);
	RootBindingCommand::SetGraphicsSRV(commandList,
		outputTransformBindingCache_.Get(outputTransformSourceSlot_), 0,
		source->GetColorTexture(0)->GetSRVGPUHandle());

	const RenderTarget& backBuffer = graphicsCore.GetBackBufferRenderTarget();
	dxCommand->BindRenderTargets(
		std::optional<RenderTarget>(backBuffer), std::nullopt);
	dxCommand->SetViewportAndScissor(backBuffer.width, backBuffer.height);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
	if (!outputTransformLogged_) {
		Logger::Output(LogType::Engine,
			"ColorPipelineの出力変換が有効です mode={} format={}",
			static_cast<uint32_t>(output.mode),
			static_cast<uint32_t>(backBuffer.format));
		outputTransformLogged_ = true;
	}
	return true;
}

Engine::ColorPipelineProcessor::ViewExposureState&
Engine::ColorPipelineProcessor::GetViewState(RenderViewKind kind) {

	return viewStates_[static_cast<size_t>(kind)];
}

Engine::ColorPipelineProcessor::ColorPipelineConstants
Engine::ColorPipelineProcessor::BuildConstants(
	GraphicsCore& graphicsCore, const SceneExecutionContext& context,
	const MultiRenderTarget& source,
	const ColorPipelineSettings& settings, bool resetExposure) const {

	ColorPipelineConstants constants{};
	constants.width = source.GetWidth();
	constants.height = source.GetHeight();
	constants.exposureMode = static_cast<uint32_t>(settings.exposure.mode);
	constants.resetExposure = resetExposure ? 1u : 0u;
	constants.manualEV100 = settings.exposure.manualEV100;
	constants.exposureCompensation = settings.exposure.compensation;
	constants.minEV100 = (std::min)(
		settings.exposure.minEV100, settings.exposure.maxEV100);
	constants.maxEV100 = (std::max)(
		settings.exposure.minEV100, settings.exposure.maxEV100);
	constants.histogramLowPercent = std::clamp(
		settings.exposure.histogramLowPercent, 0.0f, 0.99f);
	constants.histogramHighPercent = std::clamp(
		settings.exposure.histogramHighPercent,
		constants.histogramLowPercent + 0.01f, 1.0f);
	constants.speedUp = (std::max)(settings.exposure.speedUp, 0.0f);
	constants.speedDown = (std::max)(settings.exposure.speedDown, 0.0f);
	constants.deltaTime = context.systemContext ?
		(std::max)(context.systemContext->unscaledDeltaTime, 0.0f) : 0.0f;
	constants.usePreExposure = settings.exposure.usePreExposure ? 1.0f : 0.0f;
	constants.filmicSlope = (std::max)(settings.filmic.slope, 0.01f);
	constants.filmicToe = std::clamp(settings.filmic.toe, 0.0f, 1.0f);
	constants.filmicShoulder = std::clamp(settings.filmic.shoulder, 0.0f, 1.0f);
	constants.filmicBlackClip = std::clamp(settings.filmic.blackClip, 0.0f, 0.99f);
	constants.filmicWhiteClip = std::clamp(settings.filmic.whiteClip, 0.0f, 0.99f);
	constants.colorFilter = settings.colorGrading.colorFilter;
	constants.whiteBalance = CalculateWhiteBalance(
		settings.colorGrading.temperature, settings.colorGrading.tint);
	constants.saturation = settings.colorGrading.saturation;
	constants.contrast = settings.colorGrading.contrast;
	constants.gamma = settings.colorGrading.gamma;
	constants.gain = settings.colorGrading.gain;
	constants.offset = settings.colorGrading.offset;
	const DisplayOutputSettings& output =
		graphicsCore.GetDisplayOutputSettings();
	constants.outputMode = static_cast<uint32_t>(output.mode);
	constants.paperWhiteNits = output.paperWhiteNits;
	constants.maxLuminanceNits = output.maxLuminanceNits;
	return constants;
}

bool Engine::ColorPipelineProcessor::UpdateExposure(
	GraphicsCore& graphicsCore, [[maybe_unused]] const SceneExecutionContext& context,
	MultiRenderTarget& source, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, ViewExposureState& state,
	[[maybe_unused]] const ColorPipelineConstants& constants,
	D3D12_GPU_VIRTUAL_ADDRESS constantsAddress) {

	const PipelineState* pipeline = pipelineCache.GetORCreate(
		graphicsCore.GetDXObject(), assetLibrary,
		BuiltinAssets::Pipelines::AutoExposure,
		PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN,
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures());
	if (!pipeline || !pipeline->GetComputePipeline()) {
		return false;
	}

	RenderTexture2D* sourceColor = source.GetColorTexture(0);
	if (!sourceColor) {
		return false;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();
	sourceColor->Transition(*dxCommand,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	state.exposureBuffer.Transition(*dxCommand,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipeline->GetRootSignature());
	commandList->SetPipelineState(pipeline->GetComputePipeline());

	exposureBindingCache_.Sync(*pipeline);
	if (!exposureBindingCache_.Has(exposureConstantsSlot_) ||
		!exposureBindingCache_.Has(exposureSourceSlot_) ||
		!exposureBindingCache_.Has(exposureOutputSlot_)) {
		return false;
	}
	RootBindingCommand::SetComputeCBV(commandList,
		exposureBindingCache_.Get(exposureConstantsSlot_), constantsAddress);
	RootBindingCommand::SetComputeSRV(commandList,
		exposureBindingCache_.Get(exposureSourceSlot_), 0,
		sourceColor->GetSRVGPUHandle());
	RootBindingCommand::SetComputeUAV(commandList,
		exposureBindingCache_.Get(exposureOutputSlot_),
		state.exposureBuffer.GetGPUAddress(),
		state.exposureBuffer.GetUAVGPUHandle());
	commandList->Dispatch(1, 1, 1);

	const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::UAV(
		state.exposureBuffer.GetResource());
	commandList->ResourceBarrier(1, &barrier);
	state.exposureBuffer.Transition(*dxCommand,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	return true;
}

bool Engine::ColorPipelineProcessor::DrawToneMap(
	GraphicsCore& graphicsCore, MultiRenderTarget& source,
	MultiRenderTarget& dest, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, ViewExposureState& state,
	D3D12_GPU_VIRTUAL_ADDRESS constantsAddress) {

	const MaterialAsset* material = assetLibrary.LoadMaterial(
		BuiltinAssets::Materials::ToneMapToView);
	if (!material) {
		return false;
	}
	const MaterialPassBinding* pass = FindPass(*material,
		MaterialPassKind::Blit);
	if (!pass) {
		pass = FindPass(*material, MaterialPassKind::Fullscreen);
	}
	if (!pass || pass->preferredVariant == PipelineVariantKind::Compute ||
		pass->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	std::array<DXGI_FORMAT, 8> rtvFormats{};
	uint32_t formatCount = 0;
	rtvFormats.fill(DXGI_FORMAT_UNKNOWN);
	for (uint32_t index = 0; index < (std::min)(
		dest.GetColorCount(), static_cast<uint32_t>(rtvFormats.size())); ++index) {

		if (const RenderTexture2D* color = dest.GetColorTexture(index)) {
			rtvFormats[formatCount++] = color->GetFormat();
		}
	}
	const PipelineState* pipeline = pipelineCache.GetORCreate(
		graphicsCore.GetDXObject(), assetLibrary, pass->pipeline,
		pass->preferredVariant,
		std::span<const DXGI_FORMAT>(rtvFormats.data(), formatCount),
		DXGI_FORMAT_UNKNOWN);
	if (!pipeline || !pipeline->GetGraphicsPipeline(BlendMode::Normal)) {
		return false;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();
	source.TransitionForShaderRead(*dxCommand);
	state.exposureBuffer.Transition(*dxCommand,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	if (!BindColorTargetsOnly(graphicsCore, dest)) {
		return false;
	}
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetGraphicsRootSignature(pipeline->GetRootSignature());
	commandList->SetPipelineState(
		pipeline->GetGraphicsPipeline(BlendMode::Normal));

	toneMapBindingCache_.Sync(*pipeline);
	RenderTexture2D* sourceColor = source.GetColorTexture(0);
	if (!sourceColor || !toneMapBindingCache_.Has(toneMapConstantsSlot_) ||
		!toneMapBindingCache_.Has(toneMapSourceSlot_) ||
		!toneMapBindingCache_.Has(toneMapExposureSlot_)) {
		return false;
	}
	RootBindingCommand::SetGraphicsCBV(commandList,
		toneMapBindingCache_.Get(toneMapConstantsSlot_), constantsAddress);
	RootBindingCommand::SetGraphicsSRV(commandList,
		toneMapBindingCache_.Get(toneMapSourceSlot_), 0,
		sourceColor->GetSRVGPUHandle());
	RootBindingCommand::SetGraphicsSRV(commandList,
		toneMapBindingCache_.Get(toneMapExposureSlot_),
		state.exposureBuffer.GetGPUAddress(),
		state.exposureBuffer.GetSRVGPUHandle());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
	return true;
}

Engine::Vector3 Engine::ColorPipelineProcessor::CalculateWhiteBalance(
	float temperature, float tint) {

	const Vector3 reference = CalculateTemperatureRGB(6500.0f);
	Vector3 balance = CalculateTemperatureRGB(temperature);
	balance.x /= (std::max)(reference.x, 0.001f);
	balance.y /= (std::max)(reference.y, 0.001f);
	balance.z /= (std::max)(reference.z, 0.001f);
	const float tintScale = std::clamp(tint / 100.0f, -1.0f, 1.0f);
	balance.x *= 1.0f + tintScale * 0.05f;
	balance.y *= 1.0f - tintScale * 0.1f;
	balance.z *= 1.0f + tintScale * 0.05f;
	return balance;
}
