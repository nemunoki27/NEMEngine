#include "GPUParticleShapeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

bool NEMTests::CheckParticleShapePipelines(ID3D12Device* device, Engine::GraphicsResourceRetirement& retirement) {

	using namespace Engine;
	ComPtr<ID3D12Device8> device8;
	if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device8)))) return false;
	D3D12_FEATURE_DATA_D3D12_OPTIONS7 support{};
	if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &support, sizeof(support)))) return false;
	DxShaderCompiler compiler;
	compiler.Init();
	const auto root = RuntimePaths::GetEngineAssetsRoot() / "Shaders/Builtin/Particle";
	for (const char* shape : { "Ring", "Cylinder" }) {

		const std::string stem = "particle" + std::string(shape);
		const auto folder = root / "Parametric" / shape;
		GraphicsPipelineDesc desc{};
		desc.type = PipelineType::Vertex;
		desc.preRaster = { .file = Algorithm::PathToUTF8(folder / (stem + ".VS.hlsl")), .profile = "vs_6_0" };
		desc.pixel = { .file = Algorithm::PathToUTF8(root / "Default/particle.PS.hlsl"), .profile = "ps_6_0" };
		desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
		desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.depthStencil.DepthEnable = FALSE;
		desc.depthStencil.StencilEnable = FALSE;
		desc.rtvFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.staticSamplers.push_back(CD3DX12_STATIC_SAMPLER_DESC(0));
		// VSと既存Particle PSの入出力・Root Signatureを検証する
		if (!PipelineStateBuilder::CreateGraphics(retirement, device8.Get(), &compiler, desc)) return false;
		if (support.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
			desc.type = PipelineType::Mesh;
			desc.preRaster = { .file = Algorithm::PathToUTF8(folder / (stem + ".MS.hlsl")), .profile = "ms_6_6" };
			if (!PipelineStateBuilder::CreateGraphics(retirement, device8.Get(), &compiler, desc)) return false;
		}
	}
	return true;
}
