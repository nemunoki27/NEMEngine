#include "GPUFBXDrawTests.h"

//============================================================================
//	include
//============================================================================
#include "FBXImportTests.h"
#include "TestFixtures.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshGPUResourceManager.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Textures/TextureDecoder.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <iostream>

// directX
#include <d3dcompiler.h>
#include <Externals/DirectX12/d3dx12.h>

namespace {

	constexpr uint32_t kSize = 64;

	// 実際のMeshVertex配列を頂点番号で読むShaderを作る
	bool CreatePipeline(ID3D12Device* device, ComPtr<ID3D12RootSignature>& root, ComPtr<ID3D12PipelineState>& pipeline) {

		constexpr char shader[] =
			"struct Vertex { float3 normal; float3 tangent; float sign; float2 uv; float4 position; }; "
			"StructuredBuffer<Vertex> vertices : register(t0); "
			"cbuffer Projection : register(b0) { float2 center; float scale; float padding; }; "
			"struct Output { float4 position : SV_POSITION; float2 uv : TEXCOORD; }; "
			"Output VS(uint id : SV_VertexID) { Output o; Vertex v = vertices[id]; "
			"o.position = float4((v.position.xy - center) * scale, 0.5, 1); o.uv = v.uv; return o; } "
			"float4 PS(Output o) : SV_TARGET { return float4(saturate(o.uv), 1, 1); }";
		ComPtr<ID3DBlob> vertex, pixel, errors, signature;
		if (FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vertex, &errors)) ||
			FAILED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &pixel, &errors))) {
			return false;
		}
		CD3DX12_ROOT_PARAMETER parameters[2];
		parameters[0].InitAsShaderResourceView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		parameters[1].InitAsConstants(4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
		const CD3DX12_ROOT_SIGNATURE_DESC description(2, parameters);
		if (FAILED(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &errors)) ||
			FAILED(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&root)))) {
			return false;
		}
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = root.Get();
		desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
		desc.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
		desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.DepthStencilState.DepthEnable = false;
		desc.DepthStencilState.StencilEnable = false;
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline)));
	}
}

bool NEMTests::RecordFBXDraw(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList6* commands,
	Engine::SRVDescriptor& descriptors, ComPtr<ID3D12Resource>& readback) {

	using namespace Engine;
	// 転送制限があっても起動用の3画像は同時に揃う
	TextureUploadService textures;
	textures.Init(device, &descriptors);
	BuiltinTextureLibrary builtin;
	builtin.Init(textures);
	if (!builtin.GetWhiteTexture()->valid || !builtin.GetNeutralDisplacementTexture()->valid || !builtin.GetErrorTexture()->valid) {
		return false;
	}
	builtin.Finalize();
	textures.Finalize();
	TestDirectory directory("FBXDraw", RuntimePaths::GetGameAssetsRoot());
	if (!PrepareFBXTextures(directory.GetPath())) return false;
	AssetDatabase database;
	database.Init();
	const auto asset = database.ImportOrGet(RuntimePaths::ToAssetPath(directory.GetPath() / "textured.FBX"), AssetType::Mesh);
	const auto* meta = database.Find(asset);
	if (!meta || meta->type != AssetType::Mesh) return false;
	BufferUploadService uploads;
	uploads.Init(descriptors.GetRetirementQueue(), device, queue);
	MeshGPUResourceManager meshes;
	meshes.Init(device, uploads, descriptors);
	meshes.RequestMesh(database, meta->guid);
	meshes.WaitAll();
	const auto* mesh = meshes.Find(meta->guid);
	if (!mesh || !mesh->IsValid() || mesh->meshletCount == 0 || mesh->subMeshes.empty()) return false;
	// Materialが参照する実画像も通常のDecoderで読む
	TextureFileRequestDesc texture;
	texture.assetPath = mesh->subMeshes.front().defaultTextures.baseColorTexturePath;
	const auto decoded = TextureDecoder::Decode(texture);
	if (!decoded.success || decoded.image.GetPixelsSize() == 0) {
		std::cerr << "FBX texture decode failed\n";
		return false;
	}
	ComPtr<ID3D12RootSignature> root;
	ComPtr<ID3D12PipelineState> pipeline;
	if (!CreatePipeline(device, root, pipeline)) return false;
	const auto heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	const auto description = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, kSize, kSize, 1, 1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
	ComPtr<ID3D12Resource> target;
	if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &description,
		D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&target)))) return false;
	ComPtr<ID3D12DescriptorHeap> targets;
	D3D12_DESCRIPTOR_HEAP_DESC targetHeap{};
	targetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	targetHeap.NumDescriptors = 1;
	if (FAILED(device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&targets)))) return false;
	const auto handle = targets->GetCPUDescriptorHandleForHeapStart();
	device->CreateRenderTargetView(target.Get(), nullptr, handle);
	const D3D12_VIEWPORT viewport{0, 0, static_cast<float>(kSize), static_cast<float>(kSize), 0, 1};
	const D3D12_RECT scissor{0, 0, kSize, kSize};
	commands->RSSetViewports(1, &viewport);
	commands->RSSetScissorRects(1, &scissor);
	const float clear[]{0, 0, 0, 0};
	commands->ClearRenderTargetView(handle, clear, 0, nullptr);
	commands->OMSetRenderTargets(1, &handle, false, nullptr);
	commands->SetPipelineState(pipeline.Get());
	commands->SetGraphicsRootSignature(root.Get());
	commands->SetGraphicsRootShaderResourceView(0, mesh->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress());
	const float projection[]{mesh->boundsCenter.x, mesh->boundsCenter.y, 0.9f / mesh->boundsRadius, 0};
	commands->SetGraphicsRoot32BitConstants(1, 4, projection, 0);
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commands->IASetIndexBuffer(&mesh->indexBuffer.GetIndexBufferView());
	commands->DrawIndexedInstanced(mesh->lods[0].indexCount, 1, mesh->lods[0].indexOffset, 0, 0);
	// 描画完了後の画素を読戻し用Bufferへ記録する
	const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(target.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
	commands->ResourceBarrier(1, &barrier);
	D3D12_TEXTURE_COPY_LOCATION destination{};
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	uint64_t bytes = 0;
	device->GetCopyableFootprints(&description, 0, 1, 0, &destination.PlacedFootprint, nullptr, nullptr, &bytes);
	DxUtils::CreateReadbackBufferResource(device, readback, bytes);
	destination.pResource = readback.Get();
	D3D12_TEXTURE_COPY_LOCATION source{};
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.pResource = target.Get();
	commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
	// 描画提出前に所有元を終了しても資源を保持する
	meshes.Finalize();
	uploads.Finalize();
	auto& retirement = descriptors.GetRetirementQueue();
	retirement.Retire(std::move(target));
	retirement.Retire(ComPtr<ID3D12Object>(root.Get()));
	retirement.Retire(ComPtr<ID3D12Object>(pipeline.Get()));
	retirement.Retire(ComPtr<ID3D12Object>(targets.Get()));
	return true;
}

bool NEMTests::CheckFBXDraw(ID3D12Resource* readback) {

	void* mapped = nullptr;
	const D3D12_RANGE range{0, kSize * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT};
	if (!readback || FAILED(readback->Map(0, &range, &mapped))) return false;
	size_t drawn = 0;
	for (uint32_t row = 0; row < kSize; ++row) {
		const auto* pixels = static_cast<const uint8_t*>(mapped) + row * D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
		for (uint32_t column = 0; column < kSize; ++column) {
			drawn += pixels[column * 4 + 2] == 255 && pixels[column * 4 + 3] == 255;
		}
	}
	const D3D12_RANGE written{0, 0};
	readback->Unmap(0, &written);
	std::cout << "FBX GPU draw pixels=" << drawn << '\n';
	return drawn > 128 && drawn < kSize * kSize;
}
