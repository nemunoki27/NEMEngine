#include "RaytracingReflectionPass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineState.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	RaytracingReflectionPass classMethods
//============================================================================

Engine::AssetID Engine::RaytracingReflectionPass::ResolveMaterial() const {

	return
		DefaultMaterialSettings::GetInstance().
		GetRaytracingReflectionOrBuiltin();
}

void Engine::RaytracingReflectionPass::Execute(GraphicsCore& graphicsCore,
	[[maybe_unused]] const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	if (!context.resources || !context.assetDatabase || !deps_.assetLibrary ||
		!deps_.raytracingPipelineCache) {
		return;
	}

	// DeferredではLightingPassがSceneColorFinalへ照明結果を書く
	MultiRenderTarget* sceneMain = context.resources->GetSceneMain();
	MultiRenderTarget* sceneFinal = context.resources->GetSceneFinal();
	if (!sceneMain || !sceneFinal) {
		return;
	}

	// DispatchRays非対応やTLAS未構築なら反射せず、LightingPassの結果をそのまま残す
	if (!graphicsCore.GetDXObject().ShouldUseDispatchRays() || !context.raytracing.tlasResource) {
		return;
	}

	// レイトレーシングマテリアルを設定
	AssetID materialID = ResolveMaterial();
	if (!materialID) {
		return;
	}
	const MaterialAsset* material = deps_.assetLibrary->LoadMaterial(materialID);
	if (!material) {
		return;
	}
	// リフレクションパスのパイプラインを取得
	const MaterialPassBinding* passBinding = FindPass(*material, MaterialPassKind::Reflection);
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Raytracing) {
		return;
	}
	RaytracingPipelineState* pipelineState = deps_.raytracingPipelineCache->GetOrCreate(
		graphicsCore.GetDXObject(), *deps_.assetLibrary,
		passBinding->pipeline, passBinding->shaderOverride);
	if (!pipelineState) {
		return;
	}

	// TLASとビュー定数とシーン情報バッファを名前で取得する
	const RegisteredRenderBuffer* tlas = context.bufferRegistry.Find("gSceneTLAS");
	const RegisteredRenderBuffer* viewConstants = context.bufferRegistry.Find("RaytracingViewConstants");
	const RegisteredRenderBuffer* sceneInstances = context.bufferRegistry.Find("gRaytracingSceneInstances");
	const RegisteredRenderBuffer* sceneSubMeshes = context.bufferRegistry.Find("gRaytracingSubMeshes");
	const RegisteredRenderBuffer* sceneGeometries = context.bufferRegistry.Find("gRaytracingGeometries");
	// どれか1つでもなければ処理しない
	if (!tlas || !viewConstants || !sceneInstances || !sceneSubMeshes || !sceneGeometries) {
		return;
	}

	// 法線/位置/深度はGBufferから、ベース色とreflection合成先は照明済みSceneColorFinalにする
	RenderTexture2D* sourceColor = context.resources->GetGBufferAlbedo();
	DepthTexture2D* sourceDepth = sceneMain->GetDepthTexture();
	RenderTexture2D* sourceNormal = context.resources->GetGBufferNormal();
	RenderTexture2D* sourcePosition = context.resources->GetGBufferPosition();
	RenderTexture2D* sourceMaterial = context.resources->GetGBufferMaterial();
	// 反射を受けないサーフェイスの判定にマテリアルフラグを使う
	RenderTexture2D* sourceFlags = context.resources->GetGBufferFlags();
	RenderTexture2D* destColor = sceneFinal->GetColorTexture(0);
	// 入力が1つでも欠けるかUAVが無いSceneFinalなら反射できないので照明結果を残して抜ける
	if (!sourceColor || !sourceDepth || !sourceNormal || !sourcePosition ||
		!sourceMaterial || !sourceFlags || !destColor || destColor->GetUAVGPUHandle().ptr == 0) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// GBuffer入力をSRV読み取りへ、出力をUAVへ遷移する
	sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceNormal->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourcePosition->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceMaterial->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceFlags->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	// パイプライン設定
	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState1(pipelineState->GetStateObject());

	// バッファバインド
	commandList->SetComputeRootShaderResourceView(RaytracingPipelineState::kRootIndexTLAS, tlas->gpuAddress);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceColor, sourceColor->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceDepth, sourceDepth->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceNormal, sourceNormal->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourcePosition, sourcePosition->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSceneInstances, sceneInstances->srvGPUHandle);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSceneSubMeshes, sceneSubMeshes->srvGPUHandle);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSceneGeometries, sceneGeometries->srvGPUHandle);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexDestUAV, destColor->GetUAVGPUHandle());
	commandList->SetComputeRootConstantBufferView(RaytracingPipelineState::kRootIndexViewCBV, viewConstants->gpuAddress);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceFlags, sourceFlags->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(
		RaytracingPipelineState::kRootIndexSourceMaterial, sourceMaterial->GetSRVGPUHandle());

	// レイを飛ばして反射を書き込む
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = pipelineState->BuildDispatchDesc(sceneFinal->GetWidth(), sceneFinal->GetHeight(), 1);
	commandList->DispatchRays(&dispatchDesc);

	// UAV書き込み完了を待ってから後続パスが読めるSRV状態へ戻す
	dxCommand->UAVBarrier(destColor->GetResource());
	destColor->Transition(*dxCommand, static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
}
