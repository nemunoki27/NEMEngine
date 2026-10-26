#include "DecalPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>

void Engine::DecalPass::DrawImpacts(GraphicsCore& graphicsCore, SceneExecutionContext& context, const DrawConstants& draw) {

	const auto* arguments = context.bufferRegistry.Find("gGPUImpactDecalArguments");
	if (!arguments || !arguments->resource || arguments->elementCount == 0 || !impactPipeline_) return;
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) return;
	auto& platform = graphicsCore.GetDXObject();
	auto* commands = platform.GetDxCommand()->GetCommandList();
	if (!impactSignature_) {
		D3D12_INDIRECT_ARGUMENT_DESC argument{};
		argument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
		D3D12_COMMAND_SIGNATURE_DESC descriptor{};
		descriptor.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
		descriptor.NumArgumentDescs = 1;
		descriptor.pArgumentDescs = &argument;
		ComPtr<ID3D12CommandSignature> candidate;
		const HRESULT result = platform.GetDevice()->CreateCommandSignature(&descriptor, nullptr, IID_PPV_ARGS(&candidate));
		if (!DxDREDDiagnostics::CheckHRESULT(platform.GetDevice(), result, "Decal::CreateCommandSignature")) return;
		retirement_ = &platform.GetResourceRetirement();
		impactSignature_ = std::move(candidate);
	}
	commands->SetGraphicsRootSignature(impactPipeline_->GetRootSignature());
	commands->SetPipelineState(impactPipeline_->GetGraphicsPipeline(BlendMode::Normal));
	impactPipeline_->BindGlobalDescriptorTablesGraphics(commands, graphicsCore.GetSRVDescriptor().GetGPUHandle(0));
	autoBind_.Sync(*impactPipeline_, context.bufferRegistry);
	autoBind_.BindGraphics(context.bufferRegistry, commands);
	auto* surface = context.resources->GetSceneMain();
	const auto state = states_.find(context.view->GetHistoryKey());
	const auto bind = [&](const char* name, RenderTexture2D* texture) {
		if (const auto* binding = impactPipeline_->FindBindingByName(name, ShaderBindingKind::SRV)) {
			RootBindingCommand::SetGraphicsSRV(commands, binding, 0, texture->GetSRVGPUHandle());
		}
	};
	bind("gDecalPosition", surface->GetColorTexture(2));
	bind("gDecalOriginalNormal", state->second->originalNormal.get());
	bind("gDecalFlags", surface->GetColorTexture(5));
	const auto view = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), camera->matrices.viewProjectionMatrix);
	if (const auto* binding = impactPipeline_->FindBindingByName("ParticleImpactDecalViewConstants", ShaderBindingKind::CBV)) {
		RootBindingCommand::SetGraphicsCBV(commands, binding, view.gpuAddress);
	}
	const auto shading = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), draw);
	if (const auto* binding = impactPipeline_->FindBindingByName("DecalDrawConstants", ShaderBindingKind::CBV)) {
		RootBindingCommand::SetGraphicsCBV(commands, binding, shading.gpuAddress);
	}
	commands->ExecuteIndirect(impactSignature_.Get(), arguments->elementCount, arguments->resource, 0, nullptr, 0);
}
