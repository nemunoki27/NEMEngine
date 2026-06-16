#include "RaytracingReflectionPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTargetCopyUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineState.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	RaytracingReflectionPass classMethods
//============================================================================
namespace {

	bool ExecuteFullscreenBlit(Engine::GraphicsCore& graphicsCore,
		const Engine::SceneExecutionContext& context,
		Engine::MultiRenderTarget* source, Engine::MultiRenderTarget* dest,
		Engine::RenderAssetLibrary& assetLibrary, Engine::PipelineStateCache& pipelineCache,
		Engine::MaterialResolver& materialResolver,
		Engine::PipelineBindingCache& srvCache, Engine::PipelineBindingCache::SlotID srcColorSlot) {

		if (!source || !dest) {
			return false;
		}

		Engine::AssetID resolvedID = materialResolver.ResolveORDefault(
			*context.assetDatabase, {}, Engine::DefaultMaterialSlot::FullscreenCopy);
		const Engine::MaterialAsset* material = assetLibrary.LoadMaterial(resolvedID);
		if (!material) {
			return false;
		}

		const Engine::MaterialPassBinding* passBinding = FindPass(*material, Engine::MaterialPassKind::Blit);
		if (!passBinding) {
			passBinding = FindPass(*material, Engine::MaterialPassKind::Fullscreen);
		}
		if (!passBinding ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Compute ||
			passBinding->preferredVariant == Engine::PipelineVariantKind::Raytracing) {
			return false;
		}

		std::array<DXGI_FORMAT, 8> rtvFormats{};
		uint32_t numRTVFormats = 0;
		rtvFormats.fill(DXGI_FORMAT_UNKNOWN);
		for (uint32_t i = 0; i < (std::min)(dest->GetColorCount(), static_cast<uint32_t>(rtvFormats.size())); ++i) {
			if (const auto* color = dest->GetColorTexture(i)) {
				rtvFormats[numRTVFormats++] = color->GetFormat();
			}
		}

		const Engine::PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
			assetLibrary, passBinding->pipeline, passBinding->preferredVariant,
			std::span<const DXGI_FORMAT>(rtvFormats.data(), numRTVFormats), DXGI_FORMAT_UNKNOWN);
		if (!pipelineState) {
			return false;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		auto* commandList = dxCommand->GetCommandList();

		source->TransitionForShaderRead(*dxCommand);
		dest->TransitionForRender(*dxCommand);
		dest->Bind(*dxCommand);
		dxCommand->SetViewportAndScissor(dest->GetWidth(), dest->GetHeight());

		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
		commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
		commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(Engine::BlendMode::Normal));

		srvCache.Sync(*pipelineState);
		Engine::RenderTexture2D* color = source->GetColorTexture(0);
		if (!srvCache.Has(srcColorSlot) || !color) {
			return false;
		}
		Engine::RootBindingCommand::SetGraphicsSRV(commandList, srvCache.Get(srcColorSlot), 0, color->GetSRVGPUHandle());

		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->DrawInstanced(3, 1, 0, 0);
		return true;
	}
}

Engine::AssetID Engine::RaytracingReflectionPass::ResolveMaterial(AssetDatabase& database) const {

	if (materialSearched_) {
		return cachedMaterialID_;
	}
	materialSearched_ = true;

	(void)database;
	// ビルトインMaterialはパスではなく.meta GUIDで固定参照する
	cachedMaterialID_ = BuiltinAssets::Materials::RaytracingReflection;
	return cachedMaterialID_;
}

void Engine::RaytracingReflectionPass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// 全画面のreflectionなのでバケットは使わず依存が欠ける条件を弾く
	(void)passBuckets;
	if (!context.resources || !context.assetDatabase || !deps_.assetLibrary ||
		!deps_.pipelineCache || !deps_.materialResolver || !deps_.raytracingPipelineCache) {
		return;
	}

	// reflectionはSceneMainを入力にSceneFinalへ書く
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneMain || !sceneFinal) {
		return;
	}

	// raytracingを使えない/前提が崩れたときの退避でSceneMainをSceneFinalへそのまま転写する
	auto passthrough = [&]() {
		if (!ExecuteFullscreenBlit(graphicsCore, context, sceneMain, sceneFinal,
			*deps_.assetLibrary, *deps_.pipelineCache, *deps_.materialResolver,
			blitSRVCache_, srcColorSlot_)) {

			MultiRenderTargetCopy::CopyColor0Resource(graphicsCore, sceneMain, sceneFinal);
		}
		};

	// DispatchRays非対応やTLAS未構築なら反射せず素通しする
	if (!graphicsCore.GetDXObject().ShouldUseDispatchRays() || !context.raytracing.tlasResource) {
		passthrough();
		return;
	}

	AssetID materialID = ResolveMaterial(*context.assetDatabase);
	if (!materialID) {
		passthrough();
		return;
	}

	const MaterialAsset* material = deps_.assetLibrary->LoadMaterial(materialID);
	if (!material) {
		passthrough();
		return;
	}

	// Reflectionパスはraytracing variant前提で、満たさなければ素通しへ落とす
	const MaterialPassBinding* passBinding = FindPass(*material, MaterialPassKind::Reflection);
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Raytracing) {
		passthrough();
		return;
	}

	RaytracingPipelineState* pipelineState = deps_.raytracingPipelineCache->GetOrCreate(
		graphicsCore.GetDXObject(), *deps_.assetLibrary, passBinding->pipeline);
	if (!pipelineState) {
		passthrough();
		return;
	}

	// shaderが要求するTLASとview定数とシーン情報bufferを名前で引き、欠ければ素通し
	const RegisteredRenderBuffer* tlas = context.bufferRegistry.Find("gSceneTLAS");
	const RegisteredRenderBuffer* viewConstants = context.bufferRegistry.Find("RaytracingViewConstants");
	const RegisteredRenderBuffer* sceneInstances = context.bufferRegistry.Find("gRaytracingSceneInstances");
	const RegisteredRenderBuffer* sceneSubMeshes = context.bufferRegistry.Find("gRaytracingSubMeshes");
	if (!tlas || !viewConstants || !sceneInstances || !sceneSubMeshes) {
		passthrough();
		return;
	}

	// G-Buffer相当の色/深度/法線/位置を入力にしSceneFinalの色をUAV出力にする
	RenderTexture2D* sourceColor = sceneMain->GetColorTexture(0);
	DepthTexture2D* sourceDepth = sceneMain->GetDepthTexture();
	RenderTexture2D* sourceNormal = (1 < sceneMain->GetColorCount()) ? sceneMain->GetColorTexture(1) : nullptr;
	RenderTexture2D* sourcePosition = (2 < sceneMain->GetColorCount()) ? sceneMain->GetColorTexture(2) : nullptr;
	RenderTexture2D* destColor = sceneFinal->GetColorTexture(0);

	// 入力が1つでも欠けるかUAVが無いG-Bufferなら反射できないので素通し
	if (!sourceColor || !sourceDepth || !sourceNormal || !sourcePosition || !destColor ||
		destColor->GetUAVGPUHandle().ptr == 0) {
		passthrough();
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// 入力4枚をSRV読み取りへ、出力をUAVへ遷移する
	sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceNormal->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourcePosition->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// raytracing state objectを積みrootへ各リソースを束ねる
	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState1(pipelineState->GetStateObject());

	commandList->SetComputeRootShaderResourceView(RaytracingPipelineState::kRootIndexTLAS, tlas->gpuAddress);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceColor, sourceColor->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceDepth, sourceDepth->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceNormal, sourceNormal->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourcePosition, sourcePosition->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSceneInstances, sceneInstances->srvGPUHandle);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSceneSubMeshes, sceneSubMeshes->srvGPUHandle);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexDestUAV, destColor->GetUAVGPUHandle());
	commandList->SetComputeRootConstantBufferView(RaytracingPipelineState::kRootIndexViewCBV, viewConstants->gpuAddress);

	// 画面解像度ぶんのrayを飛ばして反射を書き込む
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = pipelineState->BuildDispatchDesc(
		sceneFinal->GetWidth(), sceneFinal->GetHeight(), 1);
	commandList->DispatchRays(&dispatchDesc);

	// UAV書き込み完了を待ってから後続パスが読めるSRV状態へ戻す
	dxCommand->UAVBarrier(destColor->GetResource());
	destColor->Transition(*dxCommand, static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
}
