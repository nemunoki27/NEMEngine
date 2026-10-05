#include "GPURenderTextureBindingTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameterBinder.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLookup.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <fstream>

bool NEMTests::CheckRenderTextureBindings(ID3D12Device* device, Engine::GraphicsResourceRetirement& retirement) {

	using namespace Engine;
	TestDirectory directory("RenderTextureBindings");
	const auto write = [&](const char* name, const char* source) {

		std::ofstream stream(directory.GetPath() / name, std::ios::binary);
		stream << source;
		return stream.good();
	};
	if (!write("binding.VS.hlsl", "float4 main(uint id:SV_VertexID):SV_Position { return float4(id,0,0,1); }") ||
		!write("binding.PS.hlsl", "cbuffer MaterialParameters:register(b0,space2) { uint baseColorTexture; }; "
			"float4 main():SV_Target { return float4(baseColorTexture,0,0,1); }")) return false;
	ComPtr<ID3D12Device8> device8;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device8)))) return false;
	DxShaderCompiler compiler;
	compiler.Init();
	GraphicsPipelineDesc desc{};
	desc.preRaster = { .file = Algorithm::PathToUTF8(directory.GetPath() / "binding.VS.hlsl"), .profile = "vs_6_0" };
	desc.pixel = { .file = Algorithm::PathToUTF8(directory.GetPath() / "binding.PS.hlsl"), .profile = "ps_6_0" };
	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = FALSE;
	desc.depthStencil.StencilEnable = FALSE;
	desc.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	const auto pipeline = PipelineStateBuilder::CreateGraphics(retirement, device8.Get(), &compiler, desc);
	if (!pipeline) return false;
	const AssetID textureID{ 0x1234, 0x5678 };
	MaterialAsset material;
	MaterialParameterValue textureValue;
	textureValue.value = textureID;
	material.parameters.Set(MaterialParameterIDs::BaseColorTexture, MaterialParameterNames::BaseColorTexture,
		MaterialParameterSemantic::BaseColorTexture, textureValue);
	if (!MaterialParameterLookup::ReferencesAsset(material.parameters, textureID) ||
		MaterialParameterLookup::ReferencesAsset(material.parameters, AssetID{})) return false;
	MaterialParameterBinder binder;
	binder.BeginFrame();
	uint32_t resolveCount = 0;
	bool feedback = false;
	const auto resolve = [&](MaterialParameterSemantic, const AssetID&) {

		++resolveCount;
		return MaterialParameterBufferBuilder::TextureResolveResult{ resolveCount, !feedback };
	};
	// 出力Cameraの代替値を通常Cameraの転送先へ混ぜない
	binder.SetTextureRevision(1, textureID);
	const auto restricted = binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve);
	binder.SetTextureRevision(1);
	const auto readable = binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve);
	if (!restricted || !readable || restricted == readable || resolveCount != 2) return false;
	if (binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve) != readable ||
		resolveCount != 2) return false;
	// 公開Descriptorの差替え後は同frameでも再構築する
	binder.SetTextureRevision(2);
	const auto replaced = binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve);
	if (!replaced || replaced == readable || resolveCount != 3) return false;
	// cache回収を跨いでも読取Cameraの値を維持する
	binder.SetTextureRevision(2, textureID);
	if (!binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve) || resolveCount != 4) return false;
	binder.SetTextureRevision(3, textureID);
	feedback = true;
	if (!binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve)) return false;
	binder.SetTextureRevision(3);
	feedback = false;
	if (!binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve) || resolveCount != 6) return false;
	for (uint32_t frame = 0; frame < 600; ++frame) {

		binder.BeginFrame();
		binder.SetTextureRevision(3, textureID);
		feedback = true;
		const auto blocked = binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve);
		binder.SetTextureRevision(3);
		feedback = false;
		const auto output = binder.ResolveAndUpload(retirement, device8.Get(), *pipeline, material, resolve);
		if (!blocked || !output || blocked == output || resolveCount != 7 + frame) return false;
	}
	RuntimeTextureResolver::BeginRenderTextureWrite(textureID);
	const bool writing = RuntimeTextureResolver::GetWritingRenderTexture() == textureID;
	RuntimeTextureResolver::EndRenderTextureWrite(textureID);
	return writing && !RuntimeTextureResolver::GetWritingRenderTexture() &&
		IsAssetTypeCompatible(AssetType::Texture, AssetType::RenderTexture) &&
		!IsAssetTypeCompatible(AssetType::RenderTexture, AssetType::Texture);
}
