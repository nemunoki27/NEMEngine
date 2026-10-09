#include "GlobalIlluminationView.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineBuilder.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>

//============================================================================
//	GlobalIlluminationView classMethods
//============================================================================
bool Engine::GlobalIlluminationView::EnsureResources(GraphicsCore& graphicsCore) {

	if (initialized_) return true;
	auto& platform = graphicsCore.GetDXObject();
	// Probeの蓄積には通常のCompute経路を使用
	ComputePipelineDesc blend{};
	blend.compute = { .file = "Builtin/GlobalIllumination/giProbeBlend.CS.hlsl", .entry = "main", .profile = "cs_6_0",
		.shader = BuiltinAssets::Shaders::GIProbeBlend };
	blendPipeline_ = PipelineStateBuilder::CreateCompute(platform.GetResourceRetirement(), platform.GetDevice(),
		platform.GetDxShaderCompiler(), blend);
	if (!blendPipeline_) return false;

	// 読取用と書込用のProbeを分離
	const auto create = [&](RenderTexture2D& texture, uint32_t width, uint32_t height, const wchar_t* name) {

		RenderTextureCreateDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.createUAV = true;
		desc.debugName = name;
		texture.Create(platform.GetDevice(), &graphicsCore.GetRTVDescriptor(), &graphicsCore.GetSRVDescriptor(), desc);
	};
	for (auto& field : fields_) {

		create(field.irradiance, kProbeCount, 64, L"GI照度");
		create(field.distance, kProbeCount, 64, L"GI距離");
		create(field.positions, kProbeCount, 1, L"GI位置");
		create(field.offsets, kProbeCount, 1, L"GI移動量");
	}
	create(rayResults_, 256, 128, L"GI命中結果");
	sceneBuilder_.SetGlobalIlluminationScene(true);
	sceneBuilder_.SetGlobalIlluminationMaterials(&materials_);
	sceneBuilder_.SetGlobalIlluminationGeometry(&geometry_);
	initialized_ = true;
	ClearFields(graphicsCore);
	return true;
}

void Engine::GlobalIlluminationView::ClearFields(GraphicsCore& graphicsCore) {

	// 再配置したProbeの古い照明と位置を破棄
	auto& command = *graphicsCore.GetDXObject().GetDxCommand();
	command.SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	const float zero[4]{};
	for (auto& field : fields_) {

		for (auto* texture : { &field.irradiance, &field.distance, &field.positions, &field.offsets }) {

			// 既存のRTVでProbe全体をゼロへ戻す
			texture->Transition(command, D3D12_RESOURCE_STATE_RENDER_TARGET);
			command.GetCommandList()->ClearRenderTargetView(texture->GetRenderTarget().rtvHandle, zero, 0, nullptr);
		}
	}
	nextProbe_ = 0;
	publishedPositionsValid_.fill(false);
	ready_ = false;
}

void Engine::GlobalIlluminationView::CopyField(GraphicsCore& graphicsCore, uint32_t destination) {

	// 未更新のProbeは確定結果を引き継ぐ
	auto& command = *graphicsCore.GetDXObject().GetDxCommand();
	auto& source = fields_[publishedField_];
	auto& target = fields_[destination];
	std::array<RenderTexture2D*, 4> sources = { &source.irradiance, &source.distance, &source.positions, &source.offsets };
	std::array<RenderTexture2D*, 4> targets = { &target.irradiance, &target.distance, &target.positions, &target.offsets };
	for (size_t index = 0; index < sources.size(); ++index) {

		sources[index]->Transition(command, D3D12_RESOURCE_STATE_COPY_SOURCE);
		targets[index]->Transition(command, D3D12_RESOURCE_STATE_COPY_DEST);
		command.GetCommandList()->CopyResource(targets[index]->GetResource(), sources[index]->GetResource());
		sources[index]->Transition(command, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		targets[index]->Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}
}

void Engine::GlobalIlluminationView::Release() {

	// 所有元が消えてもGPU完了までは既存の回収窓口で保持
	sceneBuilder_.Finalize();
	materials_.Clear();
	geometry_.Clear();
	graphRevision_.reset();
	pipelineGraphCount_ = UINT32_MAX;
	tracePipeline_.reset();
	blendPipeline_.reset();
	constantAllocator_.Release();
	rayResults_.Destroy();
	for (auto& field : fields_) {

		field.irradiance.Destroy();
		field.distance.Destroy();
		field.positions.Destroy();
		field.offsets.Destroy();
	}
	publishedPositionsValid_.fill(false);
	initialized_ = false;
	ready_ = false;
	frameSerial_ = UINT64_MAX;
	constantsAddress_ = 0;
	updateRecords_ = {};
}

uint64_t Engine::GlobalIlluminationView::GetMemoryBytes() const {

	return initialized_ ? uint64_t(kProbeCount) * (64 * 2 + 2) * sizeof(Vector4) * 2 + 256 * 128 * sizeof(Vector4) : 0;
}


bool Engine::GlobalIlluminationView::EnsureTracePipeline(GraphicsCore& graphicsCore) {

	if (tracePipeline_ && pipelineGraphCount_ == materials_.GetShaderCount()) return true;
	auto& platform = graphicsCore.GetDXObject();
	PipelineVariantDesc variant{};
	variant.kind = PipelineVariantKind::Raytracing;
	variant.pipelineType = PipelineType::Raytracing;
	variant.rayGenerationExports = { "GIProbeRayGen" };
	variant.missExports = { "GIMiss" };
	variant.hitGroups = { { .exportName = "GIHitGroup", .closestHitExport = "GIClosestHit", .anyHitExport = "GIAnyHit" } };
	variant.maxPayloadSizeInBytes = 48;
	variant.maxRecursionDepth = 2;
	ShaderAsset shader{};
	shader.guid = BuiltinAssets::Shaders::GIProbeTrace;
	for (const auto& entry : { "GIProbeRayGen", "GIMiss", "GIClosestHit", "GIAnyHit" }) {

		shader.stages.push_back({ .stage = ShaderStage::Lib,
			.file = "Builtin/GlobalIllumination/giProbeTrace.RT.hlsl", .entry = entry, .profile = "lib_6_6",
			.ownerShader = shader.guid });
	}
	PipelineStaticSamplerOverrideSet samplers{};
	samplers.fillMissingSamplers = true;
	PipelineStaticSamplerSettings materialSampler;
	materialSampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	materialSampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	materialSampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers.byName["gSampler"] = materialSampler;
	materials_.AppendShaders(shader, variant, samplers);
	auto pipeline = RaytracingPipelineBuilder::Create(platform.GetDevice(), platform.GetDxShaderCompiler(), variant, shader, &samplers);
	if (!pipeline) return false;
	pipeline->SetRetirementQueue(platform.GetResourceRetirement());

	tracePipeline_ = std::move(pipeline);
	pipelineGraphCount_ = materials_.GetShaderCount();
	return true;
}

Engine::GlobalIlluminationView::~GlobalIlluminationView() {

	Release();
}

const Engine::RenderTexture2D* Engine::GlobalIlluminationView::FindDiagnosticTexture(std::string_view name) const {

	if (!ready_) return nullptr;
	const auto& field = fields_[publishedField_];
	if (name == "GIIrradiance") return &field.irradiance;
	if (name == "GIDistance") return &field.distance;
	if (name == "GIPositions") return &field.positions;
	if (name == "GIOffsets") return &field.offsets;
	if (name == "GIRayResults") return &rayResults_;
	return nullptr;
}
