#include "DecalPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Decals/DecalProjection.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <algorithm>
#include <cmath>

Engine::DecalPass::DecalPass(const RenderPipelineDeps& deps) : deps_(deps) {
}

Engine::DecalPass::~DecalPass() {

	if (impactSignature_) retirement_->Retire(std::move(impactSignature_));
}

void Engine::DecalPass::RetainViews(const std::unordered_set<std::string>& activeViews) {

	// 所有Viewが消えてもResourceとDescriptorは回収窓口で保持する
	std::erase_if(states_, [&](const auto& entry) { return !activeViews.contains(entry.first); });
}

void Engine::DecalPass::EnsureNormalTarget(GraphicsCore& graphicsCore, State& state, uint32_t width, uint32_t height) {

	if (state.originalNormal && state.width == width && state.height == height) return;
	// 成功した退避先だけを公開する
	auto texture = std::make_unique<RenderTexture2D>();
	texture->Create(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetRTVDescriptor(), &graphicsCore.GetSRVDescriptor(),
		{.width = width, .height = height, .format = DXGI_FORMAT_R16G16B16A16_FLOAT, .debugName = L"DecalOriginalNormal"});
	state.originalNormal = std::move(texture);
	state.width = width;
	state.height = height;
}

void Engine::DecalPass::Execute(GraphicsCore& graphicsCore, [[maybe_unused]] const RenderPassPhaseBuckets& passBuckets,
	SceneExecutionContext& context) {

	if (!context.world || !context.view || !context.resources) return;
	const auto* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) return;
	auto& slot = states_[context.view->GetHistoryKey()];
	if (!slot) slot = std::make_unique<State>();
	auto& state = *slot;
	Prepare(graphicsCore, context, state);
	const auto* impacts = context.bufferRegistry.Find("gGPUImpactDecalArguments");
	if (state.items.empty() && (!impacts || impacts->elementCount == 0)) return;
	EnsurePipeline(graphicsCore);
	if (!pipeline_) return;
	auto* surface = context.resources->GetSceneMain();
	const uint32_t width = surface->GetWidth(), height = surface->GetHeight();
	EnsureNormalTarget(graphicsCore, state, width, height);
	auto& command = *graphicsCore.GetDXObject().GetDxCommand();
	auto* commands = command.GetCommandList();
	auto* normal = surface->GetColorTexture(1);
	// 受け取り面の法線を固定して重なり順による角度判定の変化を防ぐ
	normal->Transition(command, D3D12_RESOURCE_STATE_COPY_SOURCE);
	state.originalNormal->Transition(command, D3D12_RESOURCE_STATE_COPY_DEST);
	commands->CopyResource(state.originalNormal->GetResource(), normal->GetResource());
	state.originalNormal->Transition(command, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	const auto readState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	surface->GetColorTexture(2)->Transition(command, readState);
	surface->GetColorTexture(5)->Transition(command, readState);

	// 位置や深度やMotionを描画先へ混ぜない
	D3D12_CPU_DESCRIPTOR_HANDLE targets[4];
	const uint32_t attachments[]{0, 1, 3, 4};
	for (uint32_t index = 0; index < 4; ++index) {
		auto* texture = surface->GetColorTexture(attachments[index]);
		texture->Transition(command, D3D12_RESOURCE_STATE_RENDER_TARGET);
		targets[index] = texture->GetRenderTarget().rtvHandle;
	}
	commands->OMSetRenderTargets(4, targets, FALSE, nullptr);
	commands->SetGraphicsRootSignature(pipeline_->GetRootSignature());
	commands->SetPipelineState(pipeline_->GetGraphicsPipeline(BlendMode::Normal));
	command.SetDescriptorHeaps({graphicsCore.GetSRVDescriptor().GetDescriptorHeap()});
	pipeline_->BindGlobalDescriptorTablesGraphics(commands, graphicsCore.GetSRVDescriptor().GetGPUHandle(0));
	autoBind_.Sync(*pipeline_, context.bufferRegistry);
	autoBind_.BindGraphics(context.bufferRegistry, commands);
	const auto bind = [&](const char* name, RenderTexture2D* texture) {
		RootBindingCommand::SetGraphicsSRV(commands, pipeline_->FindBindingByName(name, ShaderBindingKind::SRV),
			0, texture->GetSRVGPUHandle());
	};
	bind("gDecalPosition", surface->GetColorTexture(2));
	bind("gDecalOriginalNormal", state.originalNormal.get());
	bind("gDecalFlags", surface->GetColorTexture(5));
	const uint32_t viewportWidth = context.useViewportRect ? context.viewportWidth : width;
	const uint32_t viewportHeight = context.useViewportRect ? context.viewportHeight : height;
	const uint32_t viewportX = context.useViewportRect ? context.viewportX : 0;
	const uint32_t viewportY = context.useViewportRect ? context.viewportY : 0;
	const D3D12_VIEWPORT viewport{static_cast<float>(viewportX), static_cast<float>(viewportY),
		static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0f, 1.0f};
	commands->RSSetViewports(1, &viewport);
	commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (constantFrameSerial_ != frameSerial) {
		constants_.BeginFrame();
		constantFrameSerial_ = frameSerial;
	}
	DrawConstants draw;
	draw.cameraPosition = camera->cameraPos;
	draw.orthographic = camera->projectionMode == ResolvedProjectionMode::Orthographic ? 1u : 0u;
	draw.pixelWorldScale = 2.0f / std::max(1e-5f, std::abs(camera->matrices.projectionMatrix.m[1][1]) * viewportHeight);
	const auto* binding = pipeline_->FindBindingByName("DecalDrawConstants", ShaderBindingKind::CBV);
	for (uint32_t index = 0; index < state.items.size(); ++index) {
		int32_t left, top, right, bottom;
		if (!DecalProjectionUtility::GetScreenRect(state.items[index].projection, camera->matrices.viewProjectionMatrix,
			viewportWidth, viewportHeight, left, top, right, bottom)) continue;
		const D3D12_RECT rectangle{left + static_cast<LONG>(viewportX), top + static_cast<LONG>(viewportY),
			right + static_cast<LONG>(viewportX), bottom + static_cast<LONG>(viewportY)};
		commands->RSSetScissorRects(1, &rectangle);
		draw.decalIndex = index;
		const auto allocation = constants_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), draw);
		RootBindingCommand::SetGraphicsCBV(commands, binding, allocation.gpuAddress);
		commands->DrawInstanced(3, 1, 0, 0);
	}
	// 後続Passへ投影のScissorを持ち越さない
	const D3D12_RECT rectangle{static_cast<LONG>(viewportX), static_cast<LONG>(viewportY),
		static_cast<LONG>(viewportX + viewportWidth), static_cast<LONG>(viewportY + viewportHeight)};
	commands->RSSetScissorRects(1, &rectangle);
	DrawImpacts(graphicsCore, context, draw);
}
