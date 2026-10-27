#include "ParticleImpactDecalRasterTests.h"
#include "ParticleGPUFixture.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUFeedbackTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>

using namespace Engine;
using namespace NEMTests;

bool NEMTests::CheckParticleImpactDecalRaster(ParticleGPUFixture& fixture, ID3D12Resource* arguments) {

	TestDirectory directory("ParticleImpactRaster");
	const auto pixelPath = directory.GetPath() / "impact.PS.hlsl";
	std::ofstream pixel(pixelPath);
	pixel << "float4 main(float4 position : SV_POSITION, nointerpolation uint slot : TEXCOORD0) : SV_TARGET0 {\n"
		"return float4(slot == 1u && position.x >= 0 ? 1 : 0,0,0,1);\n}\n";
	pixel.close();
	GraphicsPipelineDesc descriptor;
	descriptor.type = PipelineType::Vertex;
	descriptor.preRaster = {.file = Algorithm::PathToUTF8(RuntimePaths::GetEngineAssetPath("Rendering/Builtin/Decal/particleImpactDecalDraw.VS.hlsl")),
		.profile = "vs_6_0"};
	descriptor.pixel = {.file = Algorithm::PathToUTF8(std::filesystem::absolute(pixelPath)), .profile = "ps_6_0"};
	descriptor.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	descriptor.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
	descriptor.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	descriptor.depthStencil.DepthEnable = FALSE;
	descriptor.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	auto pipeline = PipelineStateBuilder::CreateGraphics(fixture.retirement, fixture.device.Get(), &fixture.compiler, descriptor);
	if (!pipeline) {
		fixture.CheckMessages();
		return false;
	}
	D3D12_INDIRECT_ARGUMENT_DESC draw{};
	draw.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
	D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
	signatureDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
	signatureDesc.NumArgumentDescs = 1;
	signatureDesc.pArgumentDescs = &draw;
	ComPtr<ID3D12CommandSignature> signature;
	if (FAILED(fixture.device.Get()->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(&signature)))) return false;

	// slot0が未使用でもslot1の投影が描画される配置にする
	std::array<ParticleImpactDecalGPU, 2> data{};
	data[1].alive = 1;
	data[1].decal.tangentX = Vector4(1, 0, 0, 0);
	data[1].decal.tangentY = Vector4(0, 1, 0, 0);
	data[1].decal.direction = Vector4(0, 0, 0.2f, 0);
	data[1].decal.worldToDecal.m[3][2] = -2.5f;
	ComPtr<ID3D12Resource> upload;
	DxUtils::CreateUploadBufferResource(fixture.device.Get(), upload, sizeof(data));
	void* mapped = nullptr;
	const D3D12_RANGE empty{};
	if (FAILED(upload->Map(0, &empty, &mapped))) return false;
	std::memcpy(mapped, data.data(), sizeof(data));
	upload->Unmap(0, nullptr);
	StructuredRWBuffer<ParticleImpactDecalGPU> decals;
	decals.Init(fixture.device.Get(), &fixture.descriptors);
	decals.EnsureCapacity(2);
	decals.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_DEST);
	auto* commands = fixture.commands.GetCommandList();
	commands->CopyBufferRegion(decals.GetResource(), 0, upload.Get(), 0, sizeof(data));
	decals.Transition(fixture.commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	RTVDescriptor targets;
	targets.Init(fixture.device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_RTV});
	targets.SetRetirementQueue(fixture.retirement);
	RenderTexture2D texture;
	texture.Create(fixture.device.Get(), &targets, &fixture.descriptors,
		{.width = 32, .height = 32, .format = DXGI_FORMAT_R32G32B32A32_FLOAT});
	const auto handle = texture.GetRenderTarget().rtvHandle;
	const float clear[4]{};
	commands->ClearRenderTargetView(handle, clear, 0, nullptr);
	commands->OMSetRenderTargets(1, &handle, FALSE, nullptr);
	const D3D12_VIEWPORT viewport{0, 0, 32, 32, 0, 1};
	const D3D12_RECT rectangle{0, 0, 32, 32};
	commands->RSSetViewports(1, &viewport);
	commands->RSSetScissorRects(1, &rectangle);
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commands->SetGraphicsRootSignature(pipeline->GetRootSignature());
	commands->SetPipelineState(pipeline->GetGraphicsPipeline(BlendMode::Normal));
	const auto matrix = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), Matrix4x4::Identity());
	RootBindingCommand::SetGraphicsCBV(commands,
		pipeline->FindBindingByName("ParticleImpactDecalViewConstants", ShaderBindingKind::CBV), matrix.gpuAddress);
	RootBindingCommand::SetGraphicsSRV(commands,
		pipeline->FindBindingByName("gGPUImpactDecals", ShaderBindingKind::SRV), decals.GetGPUAddress(), decals.GetSRVGPUHandle());
	commands->ExecuteIndirect(signature.Get(), 1, arguments, 0, nullptr, 0);

	// 本番の間接描画後に中央の1画素を読み戻す
	texture.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	ComPtr<ID3D12Resource> readback;
	DxUtils::CreateReadbackBufferResource(fixture.device.Get(), readback, 256);
	D3D12_TEXTURE_COPY_LOCATION destination{};
	destination.pResource = readback.Get();
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint.Footprint = {DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 1, 1, 256};
	D3D12_TEXTURE_COPY_LOCATION source{texture.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
	const D3D12_BOX box{16, 16, 0, 17, 17, 1};
	commands->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
	fixture.Finish();
	const D3D12_RANGE range{0, sizeof(Vector4)};
	const bool readable = SUCCEEDED(readback->Map(0, &range, &mapped));
	Vector4 actual{};
	if (readable) {
		std::memcpy(&actual, mapped, sizeof(actual));
		readback->Unmap(0, &empty);
	}
	const bool valid = readable && actual == Vector4(1, 0, 0, 1);
	if (!valid) std::cerr << "Particle impact raster: " << actual.x << "," << actual.y << "," << actual.z << "," << actual.w << '\n';
	texture.Destroy();
	fixture.Finish();
	return valid && fixture.CheckMessages();
}
