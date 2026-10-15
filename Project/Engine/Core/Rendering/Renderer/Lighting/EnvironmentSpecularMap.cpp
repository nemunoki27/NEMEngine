#include "EnvironmentSpecularMap.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>
#include "SkyboxIrradianceMap.h"

// c++
#include <algorithm>
#include <stdexcept>

//============================================================================
//	EnvironmentSpecularMap classMethods
//============================================================================
Engine::EnvironmentSpecularMap::EnvironmentSpecularMap() {

	constantsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 0, 0);
	outputSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::UAV, 0, 0);
}

Engine::EnvironmentSpecularMap::~EnvironmentSpecularMap() {

	for (auto& map : maps_) Retire(*map);
	if (brdfSRV_ != UINT32_MAX) descriptor_->Retire(brdfSRV_, brdf_);
}

void Engine::EnvironmentSpecularMap::Retire(Cubemap& map) {

	// GPU使用中の資源を回収窓口へ渡す
	if (map.srv != UINT32_MAX) descriptor_->Retire(map.srv, map.resource);
	for (uint32_t index : map.uavs) {
		if (index != UINT32_MAX) descriptor_->Retire(index, {});
	}
}

bool Engine::EnvironmentSpecularMap::Initialize(GraphicsCore& graphics) {

	if (prefilterPipeline_ && brdfPipeline_) return true;
	auto& platform = graphics.GetDXObject();
	ComputePipelineDesc desc{};
	desc.compute.file = "Builtin/Lighting/environmentSpecular.CS.hlsl";
	desc.compute.shader = BuiltinAssets::Shaders::EnvironmentSpecular;
	desc.compute.entry = "main";
	desc.compute.profile = "cs_6_0";
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	desc.staticSamplers.push_back(sampler);
	prefilterPipeline_ = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
		platform.GetDxShaderCompiler(), desc);
	desc.compute.file = "Builtin/Lighting/environmentBRDF.CS.hlsl";
	desc.compute.shader = BuiltinAssets::Shaders::EnvironmentBRDF;
	brdfPipeline_ = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
		platform.GetDxShaderCompiler(), desc);
	if (!prefilterPipeline_ || !brdfPipeline_) return false;
	descriptor_ = &graphics.GetSRVDescriptor();
	constants_.Init(platform.GetResourceRetirement(), platform.GetDevice());
	return true;
}

std::unique_ptr<Engine::EnvironmentSpecularMap::Cubemap> Engine::EnvironmentSpecularMap::CreateCubemap(
	GraphicsCore& graphics, AssetID source, uint32_t sourceSRV) {

	auto map = std::make_unique<Cubemap>();
	map->source = source;
	map->sourceSRV = sourceSRV;
	map->irradiance = std::make_unique<SkyboxIrradianceMap>();
	map->uavs.fill(UINT32_MAX);
	auto* device = graphics.GetDXObject().GetDevice();
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT,
		kFaceSize, kFaceSize, 6, kMipCount, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&map->resource));
	if (!DxDREDDiagnostics::CheckHRESULT(device, result, "EnvironmentSpecularMap::Create")) {
		throw std::runtime_error("鏡面環境光のCubemapを生成できません");
	}
	map->resource->SetName(L"EnvironmentSpecularMap");
	D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
	srv.Format = desc.Format;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srv.TextureCube.MipLevels = kMipCount;
	try {
		// 全MipのDescriptorを揃えてから所有を渡す
		descriptor_->CreateSRV(map->srv, map->resource.Get(), srv);
		for (uint32_t mip = 0; mip < kMipCount; ++mip) {
			D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
			uav.Format = desc.Format;
			uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
			uav.Texture2DArray.MipSlice = mip;
			uav.Texture2DArray.ArraySize = 6;
			descriptor_->CreateUAV(map->uavs[mip], map->resource.Get(), uav);
		}
	} catch (...) {
		if (map->srv != UINT32_MAX) descriptor_->Free(map->srv);
		for (uint32_t index : map->uavs) {
			if (index != UINT32_MAX) descriptor_->Free(index);
		}
		throw;
	}
	return map;
}

void Engine::EnvironmentSpecularMap::Bind(GraphicsCore& graphics, PipelineState& pipeline,
	const Constants& constants, uint32_t uav) {

	auto* command = graphics.GetDXObject().GetDxCommand()->GetCommandList();
	constants_.Upload(constants);
	command->SetComputeRootSignature(pipeline.GetRootSignature());
	command->SetPipelineState(pipeline.GetComputePipeline());
	pipeline.BindGlobalDescriptorTablesCompute(command, descriptor_->GetGPUHandle(0));
	bindCache_.Sync(pipeline);
	RootBindingCommand::SetComputeCBV(command, bindCache_.Get(constantsSlot_), constants_.GetGPUAddress());
	RootBindingCommand::SetComputeUAV(command, bindCache_.Get(outputSlot_), 0, descriptor_->GetGPUHandle(uav));
}

void Engine::EnvironmentSpecularMap::BuildBRDF(GraphicsCore& graphics) {

	if (brdfSRV_ != UINT32_MAX) return;
	auto& platform = graphics.GetDXObject();
	auto* device = platform.GetDevice();
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, kBRDFSize, kBRDFSize,
		1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> candidate;
	const HRESULT result = device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&candidate));
	if (!DxDREDDiagnostics::CheckHRESULT(device, result, "EnvironmentSpecularMap::BRDF")) {
		throw std::runtime_error("環境光のBRDF積分表を生成できません");
	}
	uint32_t srvIndex = UINT32_MAX;
	uint32_t uavIndex = UINT32_MAX;
	try {
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		srv.Format = desc.Format;
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srv.Texture2D.MipLevels = 1;
		descriptor_->CreateSRV(srvIndex, candidate.Get(), srv);
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
		uav.Format = desc.Format;
		uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		descriptor_->CreateUAV(uavIndex, candidate.Get(), uav);
	} catch (...) {
		if (srvIndex != UINT32_MAX) descriptor_->Free(srvIndex);
		if (uavIndex != UINT32_MAX) descriptor_->Free(uavIndex);
		throw;
	}
	// 積分表は環境画像に依存しない
	Bind(graphics, *brdfPipeline_, {UINT32_MAX, kBRDFSize, 0.0f, 512}, uavIndex);
	platform.GetDxCommand()->GetCommandList()->Dispatch((kBRDFSize + 7) / 8, (kBRDFSize + 7) / 8, 1);
	platform.GetDxCommand()->TransitionBarriers({candidate.Get()}, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
		static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	descriptor_->Retire(uavIndex, {});
	brdf_ = std::move(candidate);
	brdfSRV_ = srvIndex;
}

void Engine::EnvironmentSpecularMap::Update(GraphicsCore& graphics, AssetID source, uint32_t sourceSRV,
	const TextureOwnership& ownership) {

	selected_ = nullptr;
	if (sourceSRV == UINT32_MAX || !Initialize(graphics)) return;
	BuildBRDF(graphics);
	const uint64_t frame = GraphicsFrameState::GetFrameSerial();
	for (auto& map : maps_) {
		if (map->source == source && map->sourceSRV == sourceSRV && map->sourceLifetime.lock() == ownership) selected_ = map.get();
	}
	if (!selected_) {
		maps_.push_back(CreateCubemap(graphics, source, sourceSRV));
		selected_ = maps_.back().get();
		selected_->sourceLifetime = ownership;
	}
	auto& map = *selected_;
	map.lastUsed = frame;
	if (map.nextMip == kMipCount || map.updateFrame == frame) return;
	map.updateFrame = frame;
	map.irradiance->Update(graphics, source, sourceSRV);
	const uint32_t mip = map.nextMip;
	const uint32_t size = std::max(kFaceSize >> mip, 1u);
	Bind(graphics, *prefilterPipeline_, {sourceSRV, size, float(mip) / float(kMipCount - 1), 128}, map.uavs[mip]);
	auto* dxCommand = graphics.GetDXObject().GetDxCommand();
	dxCommand->GetCommandList()->Dispatch((size + 7) / 8, (size + 7) / 8, 6);
	++map.nextMip;
	if (map.nextMip == kMipCount) {
		// 未完成のMipを描画へ公開しない
		dxCommand->TransitionBarriers({map.resource.Get()}, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
		for (uint32_t& index : map.uavs) {
			descriptor_->Retire(index, {});
			index = UINT32_MAX;
		}
	}
}

uint32_t Engine::EnvironmentSpecularMap::GetCubemapIndex() const {

	if (!selected_) return UINT32_MAX;
	if (selected_->nextMip == kMipCount) return selected_->srv;
	// 新しい世代が完成するまでは同じAssetの旧結果を使う
	for (const auto& map : maps_) {
		if (map->source == selected_->source && map->nextMip == kMipCount) return map->srv;
	}
	return UINT32_MAX;
}

uint32_t Engine::EnvironmentSpecularMap::GetIrradianceIndex() const {

	return selected_ ? selected_->irradiance->GetSRVIndex() : UINT32_MAX;
}

void Engine::EnvironmentSpecularMap::CollectUnused() {

	const uint64_t frame = GraphicsFrameState::GetFrameSerial();
	for (auto iterator = maps_.begin(); iterator != maps_.end();) {
		if (HasExpiredGraphicsResource((*iterator)->lastUsed, frame)) {
			if (selected_ == iterator->get()) selected_ = nullptr;
			Retire(**iterator);
			iterator = maps_.erase(iterator);
		} else ++iterator;
	}
}
