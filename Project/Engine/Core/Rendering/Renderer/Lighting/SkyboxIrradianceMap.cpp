#include "SkyboxIrradianceMap.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	SkyboxIrradianceMap classMethods
//============================================================================

Engine::SkyboxIrradianceMap::SkyboxIrradianceMap() {

	constantsSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 0, 0);
	outputUAVSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::UAV, 0, 0);
}

void Engine::SkyboxIrradianceMap::EnsureResources(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	auto& platform = graphicsCore.GetDXObject();
	ID3D12Device8* device = platform.GetDevice();

	// 畳み込みコンピュートパイプラインを生成
	ComputePipelineDesc desc{};
	desc.compute.file = "Builtin/Lighting/skyboxIrradiance.CS.hlsl";
	desc.compute.shader = BuiltinAssets::Shaders::SkyboxIrradiance;
	desc.compute.entry = "main";
	desc.compute.profile = "cs_6_6";

	// cubemapサンプリング用の静的サンプラー
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderRegister = 0;
	sampler.RegisterSpace = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	desc.staticSamplers.push_back(sampler);

	pipeline_ = PipelineStateBuilder::CreateCompute(device, platform.GetDxShaderCompiler(), desc);
	if (!pipeline_) {
		return;
	}

	// 放射照度cubemapリソースを生成
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = kFaceSize;
	resourceDesc.Height = kFaceSize;
	resourceDesc.DepthOrArraySize = 6;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	HRESULT hr = device->CreateCommittedResource(&heapProperties, D3D12_HEAP_FLAG_NONE,
		&resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&cubemap_));
	Assert::Call(SUCCEEDED(hr), "SkyboxのIrradiance Cubemap作成に失敗しました");
	cubemap_->SetName(L"SkyboxIrradianceMap");
	cubemapState_ = D3D12_RESOURCE_STATE_COMMON;

	SRVDescriptor& srvDescriptor = graphicsCore.GetSRVDescriptor();

	// SRVはcubemapとして生成
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = resourceDesc.Format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
	srvDesc.TextureCube.MostDetailedMip = 0;
	srvDesc.TextureCube.MipLevels = 1;
	srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	srvDescriptor.CreateSRV(srvIndex_, cubemap_.Get(), srvDesc);

	// UAVは6面のTexture2DArrayとして生成
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = resourceDesc.Format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
	uavDesc.Texture2DArray.MipSlice = 0;
	uavDesc.Texture2DArray.FirstArraySlice = 0;
	uavDesc.Texture2DArray.ArraySize = 6;
	srvDescriptor.CreateUAV(uavIndex_, cubemap_.Get(), uavDesc);

	// 畳み込み定数バッファを生成
	constants_.CreateBuffer(device);

	initialized_ = true;
}

void Engine::SkyboxIrradianceMap::Update(GraphicsCore& graphicsCore,
	AssetID sourceAssetID, uint32_t sourceSRVIndex) {

	// 構築済みで元cubemapが変わっていなければ何もしない
	if (built_ && builtAssetID_ == sourceAssetID && builtSRVIndex_ == sourceSRVIndex) {
		return;
	}

	EnsureResources(graphicsCore);
	if (!initialized_) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// 畳み込み定数を転送
	IrradianceConstants constants{};
	constants.sourceCubemapIndex = sourceSRVIndex;
	constants.faceSize = kFaceSize;
	constants_.TransferData(constants);

	// 出力cubemapをUAV状態へ遷移
	if (cubemapState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {

		dxCommand->TransitionBarriers({ cubemap_.Get() },
			cubemapState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		cubemapState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// パイプラインを設定
	commandList->SetComputeRootSignature(pipeline_->GetRootSignature());
	commandList->SetPipelineState(pipeline_->GetComputePipeline());

	// 定数と出力UAVをバインド
	bindCache_.Sync(*pipeline_);
	if (bindCache_.Has(constantsSlot_)) {
		RootBindingCommand::SetComputeCBV(commandList, bindCache_.Get(constantsSlot_),
			constants_.GetResource()->GetGPUVirtualAddress());
	}
	if (bindCache_.Has(outputUAVSlot_)) {
		RootBindingCommand::SetComputeUAV(commandList, bindCache_.Get(outputUAVSlot_),
			0, graphicsCore.GetSRVDescriptor().GetGPUHandle(uavIndex_));
	}

	// 6面へ畳み込みを実行
	const uint32_t groupCount = (kFaceSize + 7) / 8;
	commandList->Dispatch(groupCount, groupCount, 6);

	// 書き込み完了を待ってからシェーダーリード状態へ遷移
	dxCommand->UAVBarrier(cubemap_.Get());
	dxCommand->TransitionBarriers({ cubemap_.Get() },
		cubemapState_, static_cast<D3D12_RESOURCE_STATES>(
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	cubemapState_ = static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	// 構築済みの元cubemapを記録
	builtAssetID_ = sourceAssetID;
	builtSRVIndex_ = sourceSRVIndex;
	built_ = true;
}
