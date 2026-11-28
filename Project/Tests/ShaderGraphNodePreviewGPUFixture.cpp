#include "ShaderGraphNodePreviewGPUFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTexture2D.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphFrameConstants.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <cstring>
#include <fstream>
#include <stdexcept>
// directX
#include <Externals/DirectX12/d3dx12.h>

using namespace Engine;
using namespace NEMTests;

ShaderGraphNodePreviewGPUFixture::ShaderGraphNodePreviewGPUFixture()
	: directory_("ShaderGraphNodePreviewGPU") {

	targets_.Init(gpu_.device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_RTV});
	targets_.SetRetirementQueue(gpu_.retirement);
	color_.Create(gpu_.device.Get(), &targets_, &gpu_.descriptors,
		{.width = GetSize(), .height = GetSize(), .format = DXGI_FORMAT_R32G32B32A32_FLOAT,
		.useOptimizedClearValue = false});
	CreateInputs();
}

ShaderGraphNodePreviewGPUFixture::~ShaderGraphNodePreviewGPUFixture() {

	gpu_.Finish();
	color_.Destroy();
	texture_.Destroy();
	for (auto& texture : sceneTextures_) texture.Destroy();
	gpu_.Finish();
}

std::vector<Vector4> ShaderGraphNodePreviewGPUFixture::Render(const ShaderGraphNodePreviewCompileOutput& output,
	float time, float deltaTime, float smoothDeltaTime, const ShaderGraphTimeConstantsGPU* frameOverride,
	uint32_t imageSize, const Vector4& clearColor) {

	if (!output.Succeeded() || output.pixelHLSL.empty()) {
		throw std::runtime_error("ノード評価Shaderを生成できませんでした");
	}
	if (color_.GetResource()->GetDesc().Width != imageSize) {
		gpu_.Finish();
		color_.Destroy();
		color_.Create(gpu_.device.Get(), &targets_, &gpu_.descriptors,
			{.width = imageSize, .height = imageSize, .format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.useOptimizedClearValue = false});
	}
	const auto path = directory_.GetPath() / ("preview" + std::to_string(shaderIndex_++) + ".PS.hlsl");
	std::ofstream stream(path, std::ios::binary);
	stream << output.pixelHLSL;
	stream.close();
	GraphicsPipelineDesc desc;
	desc.preRaster.file = Algorithm::PathToUTF8(RuntimePaths::GetEngineAssetPath(
		"Rendering/Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl"));
	desc.preRaster.profile = "vs_6_0";
	desc.pixel.file = Algorithm::PathToUTF8(path);
	desc.pixel.profile = "ps_6_0";
	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.disableNormalBlending = true;
	desc.normalBlendOnly = true;
	desc.staticSamplers.emplace_back(CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP));
	for (const auto& sampler : output.samplers) {
		desc.staticSamplerOverrides.byName.emplace(sampler.shaderName, sampler.settings);
	}
	ShaderAsset metadata;
	metadata.parameters = output.parameters;
	auto pipeline = PipelineStateBuilder::CreateGraphics(gpu_.retirement, gpu_.device.Get(), &gpu_.compiler, desc, &metadata);
	if (!pipeline) throw std::runtime_error("ノード評価Pipelineを作成できませんでした");
	MaterialParameterLayout layout;
	layout.Build(pipeline->GetGraphicsReflection());
	const auto parameters = MaterialParameterBufferBuilder::BuildElement(output.defaultParameters, {}, layout,
		[&](MaterialParameterSemantic, const AssetID& id) {
			return MaterialParameterBufferBuilder::TextureResolveResult{
				.index = id == GetTextureID() ? texture_.GetSRVIndex() : kNoTextureIndex,
			};
		});

	color_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
	auto* commands = gpu_.commands.GetCommandList();
	const auto target = color_.GetRenderTarget().rtvHandle;
	const float clear[4]{clearColor.x, clearColor.y, clearColor.z, clearColor.w};
	commands->ClearRenderTargetView(target, clear, 0, nullptr);
	commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
	const Vector4 region = frameOverride ? frameOverride->viewport :
		Vector4(0, 0, static_cast<float>(imageSize), static_cast<float>(imageSize));
	const D3D12_VIEWPORT viewport{region.x, region.y, region.z, region.w, 0, 1};
	const D3D12_RECT rectangle{static_cast<LONG>(region.x), static_cast<LONG>(region.y),
		static_cast<LONG>(region.x + region.z), static_cast<LONG>(region.y + region.w)};
	commands->RSSetViewports(1, &viewport);
	commands->RSSetScissorRects(1, &rectangle);
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commands->SetGraphicsRootSignature(pipeline->GetRootSignature());
	commands->SetPipelineState(pipeline->GetGraphicsPipeline(BlendMode::Normal));
	pipeline->BindGlobalDescriptorTablesGraphics(commands, gpu_.descriptors.GetDescriptorHeap()->GetGPUDescriptorHandleForHeapStart());
	const auto bindConstants = [&](uint32_t slot, auto values) {
		if (const auto* binding = pipeline->FindBinding(ShaderBindingKind::CBV, slot)) {
			const auto allocation = gpu_.constants.AllocateAndUpload(gpu_.retirement, gpu_.device.Get(), values);
			RootBindingCommand::SetGraphicsCBV(commands, binding, allocation.gpuAddress);
		}
	};
	if (const auto* binding = pipeline->FindBinding(ShaderBindingKind::CBV, 0)) {
		const auto allocation = gpu_.constants.AllocateAndUploadBytes(gpu_.retirement, gpu_.device.Get(), parameters);
		RootBindingCommand::SetGraphicsCBV(commands, binding, allocation.gpuAddress);
	}
	bindConstants(1, Vector4(0, 1, float(GetSize()), 0));
	ShaderGraphTimeConstantsGPU frame;
	frame.time = time;
	frame.deltaTime = deltaTime;
	frame.smoothDeltaTime = smoothDeltaTime;
	frame.unscaledTime = time;
	frame.screenSize.x = frame.screenSize.y = static_cast<float>(imageSize);
	frame.viewport = region;
	if (frameOverride) frame = *frameOverride;
	bindConstants(4, frame);
	for (uint32_t index = 0; index < sceneTextures_.size(); ++index) {
		if (const auto* binding = pipeline->FindBinding(ShaderBindingKind::SRV, index, 4)) {
			RootBindingCommand::SetGraphicsSRV(commands, binding, 0, sceneTextures_[index].GetSRVGPUHandle());
		}
	}
	commands->DrawInstanced(3, 1, 0, 0);
	return ReadOutput();
}

bool ShaderGraphNodePreviewGPUFixture::CheckMessages() {

	return gpu_.CheckMessages();
}

void ShaderGraphNodePreviewGPUFixture::CreateInputs() {

	texture_.Create(gpu_.device.Get(), &targets_, &gpu_.descriptors,
		{.width = 2, .height = 2, .format = DXGI_FORMAT_R32G32B32A32_FLOAT});
	texture_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_COPY_DEST);
	const auto resourceDesc = texture_.GetResource()->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT64 bytes = 0;
	gpu_.device.Get()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
	ComPtr<ID3D12Resource> upload;
	DxUtils::CreateUploadBufferResource(gpu_.device.Get(), upload, bytes);
	void* mapped = nullptr;
	const D3D12_RANGE empty{};
	if (FAILED(upload->Map(0, &empty, &mapped))) throw std::runtime_error("検証Textureの転送に失敗しました");
	const std::array<Vector4, 4> pixels{{{1, 0, 0, .25f}, {0, 1, 0, .5f}, {0, 0, 1, .75f}, {1, 1, 1, 1}}};
	for (uint32_t y = 0; y < 2; ++y) {
		std::memcpy(static_cast<uint8_t*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch,
			pixels.data() + y * 2, sizeof(Vector4) * 2);
	}
	upload->Unmap(0, nullptr);
	D3D12_TEXTURE_COPY_LOCATION destination{texture_.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
	D3D12_TEXTURE_COPY_LOCATION source{upload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
	source.PlacedFootprint = footprint;
	gpu_.commands.GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	texture_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	ComPtr<ID3D12Resource> flagsUpload;
	for (uint32_t index = 0; index < sceneTextures_.size(); ++index) {
		const auto format = index == 6 ? DXGI_FORMAT_R32_UINT : index == 1 ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R32G32B32A32_FLOAT;
		sceneTextures_[index].Create(gpu_.device.Get(), &targets_, &gpu_.descriptors,
			{.width = index == 6 ? 32u : GetSize(), .height = index == 6 ? 24u : GetSize(), .format = format});
		if (index == 6) {
			sceneTextures_[index].Transition(gpu_.commands, D3D12_RESOURCE_STATE_COPY_DEST);
			const auto flagsDesc = sceneTextures_[index].GetResource()->GetDesc();
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT flagsFootprint{};
			UINT64 flagsBytes = 0;
			gpu_.device.Get()->GetCopyableFootprints(&flagsDesc, 0, 1, 0, &flagsFootprint, nullptr, nullptr, &flagsBytes);
			DxUtils::CreateUploadBufferResource(gpu_.device.Get(), flagsUpload, flagsBytes);
			if (FAILED(flagsUpload->Map(0, &empty, &mapped))) throw std::runtime_error("検証Flagsの転送に失敗しました");
			for (uint32_t y = 0; y < 24; ++y) {
				for (uint32_t x = 0; x < 32; ++x) {
					const uint32_t value = x + y * 32;
					std::memcpy(static_cast<uint8_t*>(mapped) + flagsFootprint.Offset +
						y * flagsFootprint.Footprint.RowPitch + x * sizeof(value), &value, sizeof(value));
				}
			}
			flagsUpload->Unmap(0, nullptr);
			D3D12_TEXTURE_COPY_LOCATION flagsDestination{sceneTextures_[index].GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
			D3D12_TEXTURE_COPY_LOCATION flagsSource{flagsUpload.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
			flagsSource.PlacedFootprint = flagsFootprint;
			gpu_.commands.GetCommandList()->CopyTextureRegion(&flagsDestination, 0, 0, 0, &flagsSource, nullptr);
			sceneTextures_[index].Transition(gpu_.commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			continue;
		}
		sceneTextures_[index].Transition(gpu_.commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
		const float clear[4]{index == 1 ? ViewDepth::ToDeviceDepth(ViewDepth::kDSVFormat, .25f) : .25f, .5f, .75f, 1};
		gpu_.commands.GetCommandList()->ClearRenderTargetView(sceneTextures_[index].GetRenderTarget().rtvHandle, clear, 0, nullptr);
		sceneTextures_[index].Transition(gpu_.commands, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	}
	gpu_.Finish();
}

std::vector<Vector4> ShaderGraphNodePreviewGPUFixture::ReadOutput() {

	color_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	const auto resourceDesc = color_.GetResource()->GetDesc();
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT64 bytes = 0;
	gpu_.device.Get()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
	ComPtr<ID3D12Resource> readback;
	DxUtils::CreateReadbackBufferResource(gpu_.device.Get(), readback, bytes);
	D3D12_TEXTURE_COPY_LOCATION destination{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
	destination.PlacedFootprint = footprint;
	D3D12_TEXTURE_COPY_LOCATION source{color_.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
	gpu_.commands.GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	gpu_.Finish();
	void* mapped = nullptr;
	const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
	if (FAILED(readback->Map(0, &range, &mapped))) throw std::runtime_error("ノード評価結果の読戻しに失敗しました");
	const auto width = static_cast<uint32_t>(resourceDesc.Width);
	std::vector<Vector4> result(static_cast<size_t>(width) * resourceDesc.Height);
	for (uint32_t y = 0; y < resourceDesc.Height; ++y) {
		std::memcpy(result.data() + y * width, static_cast<uint8_t*>(mapped) + footprint.Offset +
			y * footprint.Footprint.RowPitch, sizeof(Vector4) * width);
	}
	const D3D12_RANGE empty{};
	readback->Unmap(0, &empty);
	return result;
}
