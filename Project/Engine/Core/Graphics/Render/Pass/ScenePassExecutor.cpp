#include "ScenePassExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Render/View/RenderViewResolver.h>
#include <Engine/Core/Graphics/Render/RenderPipelineRunner.h>
#include <Engine/Core/Scene/Instance/SceneInstanceManager.h>
#include <Engine/Core/Graphics/Pipeline/Bind/ComputeRootBinder.h>
#include <Engine/Core/Graphics/DxLib/DxUtils.h>

//============================================================================
//	ScenePassExecutor classMethods
//============================================================================

namespace {

	// サーフェイスからカラーフォーマットを収集する
	void FillColorFormats(const Engine::MultiRenderTarget* surface,
		std::array<DXGI_FORMAT, 8>& outFormats, uint32_t& outCount) {

		outFormats.fill(DXGI_FORMAT_UNKNOWN);
		outCount = 0;
		if (!surface) {
			return;
		}
		const uint32_t colorCount = (std::min)(surface->GetColorCount(), static_cast<uint32_t>(outFormats.size()));
		for (uint32_t i = 0; i < colorCount; ++i) {
			if (const auto* color = surface->GetColorTexture(i)) {

				outFormats[outCount++] = color->GetFormat();
			}
		}
	}
	// サーフェイスから深度フォーマットを収集する
	DXGI_FORMAT GatherDepthFormat(const Engine::MultiRenderTarget* surface) {

		if (!surface) {
			return DXGI_FORMAT_UNKNOWN;
		}
		if (const auto* depth = surface->GetDepthTexture()) {
			return depth->GetDSVFormat();
		}
		return DXGI_FORMAT_UNKNOWN;
	}
	// コンピュートパスのバインドアイテムに描画バッファのバインディングを追加する
	void AppendComputeBufferBindings(const Engine::RenderBufferRegistry& registry,
		const Engine::PipelineState& pipelineState, std::vector<Engine::ComputeBindItem>& outBindItems) {

		const auto& entries = registry.GetEntries();
		outBindItems.reserve(outBindItems.size() + entries.size() * 4);
		for (const Engine::RegisteredRenderBuffer& entry : entries) {
			// CBVはGPUアドレスが有効な場合のみバインド
			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::CBV)) {
				if (entry.gpuAddress != 0) {

					outBindItems.push_back({ entry.alias,Engine::ComputeBindValueType::CBV,entry.gpuAddress, });
				}
			}
			// SRVはGPUアドレスまたはSRVハンドルが有効な場合にバインド
			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::SRV)) {
				if (entry.gpuAddress != 0 || entry.srvGPUHandle.ptr != 0) {

					outBindItems.push_back({ entry.alias,Engine::ComputeBindValueType::SRV,
						entry.gpuAddress,entry.srvGPUHandle, });
				}
			}
			// UAVはGPUアドレスまたはUAVハンドルが有効な場合にバインド
			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::UAV)) {
				if (entry.gpuAddress != 0 || entry.uavGPUHandle.ptr != 0) {

					outBindItems.push_back({ entry.alias,Engine::ComputeBindValueType::UAV,
						entry.gpuAddress,entry.uavGPUHandle, });
				}
			}
			// アクセル構造体はGPUアドレスが有効な場合にバインド
			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::AccelStruct)) {
				if (entry.gpuAddress != 0) {

					outBindItems.push_back({ entry.alias,Engine::ComputeBindValueType::AccelStruct,entry.gpuAddress, });
				}
			}
		}
	}
	// パイプラインが使うUAVバッファのGPUリソースを集める
	std::vector<ID3D12Resource*> GatherRegistryUAVResources(
		const Engine::RenderBufferRegistry& registry, const Engine::PipelineState& pipelineState) {

		std::vector<ID3D12Resource*> result{};
		const auto& entries = registry.GetEntries();
		result.reserve(entries.size());
		for (const Engine::RegisteredRenderBuffer& entry : entries) {
			if (!entry.resource) {
				continue;
			}
			if (pipelineState.FindBindingByName(entry.alias, Engine::ShaderBindingKind::UAV)) {
				result.emplace_back(entry.resource);
			}
		}
		return result;
	}
}

void Engine::ScenePassExecutor::ExecuteScene(GraphicsCore& graphicsCore,
	const RenderFrameRequest& request, const SceneExecutionContext& context,
	const RenderPassPhaseBuckets* prebuiltPassBuckets) {

	if (!context.sceneInstance) {
		return;
	}

	const SceneHeader& header = context.sceneInstance->header;

	// デフォルトサーフェスがある場合、シーンパスで明示的にクリアされないならクリアする
	if (context.defaultSurface && !HasExplicitClearForDefaultSurface(context, header)) {

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

		MultiRenderTargetClearDesc clearDesc{};
		clearDesc.clearColor = true;
		clearDesc.clearDepth = (context.defaultSurface->GetDepthTexture() != nullptr);
		clearDesc.clearStencil = false;

		// デフォルトサーフェイスをレンダーターゲット用に遷移してバインドし、クリアする
		context.defaultSurface->TransitionForRender(*dxCommand);
		context.defaultSurface->Bind(*dxCommand);
		context.defaultSurface->Clear(*dxCommand, clearDesc);
	}

	// 描画アイテムを描画フェーズごとに振り分けるバケットを構築する
	RenderPassPhaseBuckets passBuckets{};
	const RenderPassPhaseBuckets* activePassBuckets = prebuiltPassBuckets;
	if (!activePassBuckets && context.view) {

		UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
		RenderPassItemCollector::BuildBucketsForViewAndScene(*dependencies.renderBatch, *context.view, sceneInstanceID, passBuckets);
		activePassBuckets = &passBuckets;
	}
	if (!activePassBuckets) {
		activePassBuckets = &passBuckets;
	}

	// シーンパスの実行
	for (const auto& pass : header.passOrder) {
		switch (pass.type) {
		case ScenePassType::Clear:

			ExecuteClearPass(graphicsCore, context, pass.clear);
			break;
		case ScenePassType::DepthPrepass:

			ExecuteDepthPrepassPass(graphicsCore, context, pass.depthPrepass, *activePassBuckets);
			break;
		case ScenePassType::Draw:

			ExecuteDrawPass(graphicsCore, context, pass.draw, *activePassBuckets);
			break;
		case ScenePassType::PostProcess:

			ExecutePostProcessPass(graphicsCore, context, pass.postProcess);
			break;
		case ScenePassType::Compute:

			ExecuteComputePass(graphicsCore, context, pass.compute);
			break;
		case ScenePassType::RenderScene:

			ExecuteRenderScenePass(graphicsCore, request, context, pass.renderScene);
			break;
		case ScenePassType::Blit:

			ExecuteBlitPass(graphicsCore, context, pass.blit);
			break;
		case ScenePassType::Raytracing:

			ExecuteRaytracingPass(graphicsCore, context, pass.raytracing);
			break;
		}
	}
}

bool Engine::ScenePassExecutor::ExecuteFullscreenMaterial(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
	MultiRenderTarget* source, MultiRenderTarget* dest, AssetID requestedMaterial, DefaultMaterialSlot defaultSlot,
	const std::string_view& passName) const {

	// 入出力がない場合は実行できない
	if (!source || !dest) {
		return false;
	}

	// マテリアルを取得して読み込む
	AssetID resolvedMaterialID = dependencies.materialResolver->ResolveORDefault(
		*context.assetDatabase, requestedMaterial, defaultSlot);
	const MaterialAsset* material = dependencies.assetLibrary->LoadMaterial(resolvedMaterialID);
	if (!material) {
		return false;
	}

	// パスを探す
	const MaterialPassBinding* passBinding = FindPass(*material, passName);
	if (!passBinding) {
		passBinding = FindPass(*material, "Fullscreen");
	}
	if (!passBinding) {
		return false;
	}
	// 無効なパスは処理しない
	if (passBinding->preferredVariant == PipelineVariantKind::Compute ||
		passBinding->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	// 描画フォーマット取得
	std::array<DXGI_FORMAT, 8> rtvFormats{};
	uint32_t numRTVFormats = 0;
	FillColorFormats(dest, rtvFormats, numRTVFormats);
	// パイプライン取得
	const PipelineState* pipelineState = dependencies.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*dependencies.assetLibrary, passBinding->pipeline, passBinding->preferredVariant,
		std::span<const DXGI_FORMAT>(rtvFormats.data(), numRTVFormats), GatherDepthFormat(dest));

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// ソースをシェーダーリード用に遷移、宛先をレンダーターゲット用に遷移してバインド
	source->TransitionForShaderRead(*dxCommand);
	dest->TransitionForRender(*dxCommand);
	dest->Bind(*dxCommand);
	dxCommand->SetViewportAndScissor(dest->GetWidth(), dest->GetHeight());

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// パイプラインを設定
	commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(BlendMode::Normal));

	if (const RootBindingLocation* sourceColorBinding = pipelineState->FindBinding(ShaderBindingKind::SRV, 0, 0)) {

		RenderTexture2D* sourceColor = source->GetColorTexture(0);
		if (!sourceColor) {
			return false;
		}
		commandList->SetGraphicsRootDescriptorTable(sourceColorBinding->rootParameterIndex, sourceColor->GetSRVGPUHandle());
	}
	if (const RootBindingLocation* sourceDepthBinding = pipelineState->FindBinding(ShaderBindingKind::SRV, 1, 0)) {

		DepthTexture2D* sourceDepth = source->GetDepthTexture();
		if (!sourceDepth) {
			return false;
		}
		commandList->SetGraphicsRootDescriptorTable(sourceDepthBinding->rootParameterIndex, sourceDepth->GetSRVGPUHandle());
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);

	return true;
}

void Engine::ScenePassExecutor::TransitionAllTargetsToShaderRead(
	GraphicsCore& graphicsCore, const SceneExecutionContext& context) const {

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	for (MultiRenderTarget* surface : context.targetRegistry->GatherUniqueSurfaces()) {

		surface->TransitionForShaderRead(*dxCommand);
	}
}

void Engine::ScenePassExecutor::ExecuteClearPass(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const ClearPassDesc& pass) const {

	// レンダーターゲットを適用
	MultiRenderTarget* surface = ApplyRenderTargets(graphicsCore, context, pass.dest);
	if (!surface) {
		return;
	}

	// クリアの内容を構築
	MultiRenderTargetClearDesc clearDesc{};
	clearDesc.clearColor = pass.clearColor;
	clearDesc.clearColorValue = pass.clearColorValue;
	clearDesc.clearDepth = pass.clearDepth;
	clearDesc.clearDepthValue = pass.clearDepthValue;
	clearDesc.clearStencil = pass.clearStencil;
	clearDesc.clearStencilValue = pass.clearStencilValue;

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	surface->Clear(*dxCommand, clearDesc);
}

void Engine::ScenePassExecutor::ExecuteDepthPrepassPass(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const DepthPrepassPassDesc& pass,
	const RenderPassPhaseBuckets& passBuckets) const {

	// サーフェイスを適用、深度テクスチャがないなら処理しない
	MultiRenderTarget* surface = context.targetRegistry->Resolve(pass.dest);
	if (!surface) {
		surface = context.defaultSurface;
	}
	if (!surface) {
		return;
	}
	if (!surface->GetDepthTexture()) {
		return;
	}
	// 深度プリパス用のアイテムを収集、アイテムがないなら処理しない
	std::vector<const RenderItem*> items = CollectDepthPrepassItems(context, pass, passBuckets);
	if (items.empty()) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();

	// 深度書き込み状態に遷移する
	surface->GetDepthTexture()->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
	// レンダーターゲットをバインド
	dxCommand->BindRenderTargets(std::nullopt, surface->GetDepthTexture()->GetDSVCPUHandle());
	dxCommand->SetViewportAndScissor(surface->GetWidth(), surface->GetHeight());

	// アイテムを描画
	std::string_view passName = pass.passName.empty() ? std::string_view("ZPrepass") : std::string_view(pass.passName);
	dependencies.dispatcher->Dispatch(graphicsCore, context, *dependencies.renderBatch,
		*dependencies.backendRegistry, *dependencies.assetLibrary, *dependencies.pipelineCache,
		*dependencies.materialResolver, items, surface, passName, true);
}

void Engine::ScenePassExecutor::ExecuteDrawPass(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const DrawPassDesc& pass,
	const RenderPassPhaseBuckets& passBuckets) const {

	// レンダーターゲットを適用
	MultiRenderTarget* surface = ApplyRenderTargets(graphicsCore, context, pass.dest);
	if (!surface) {
		return;
	}

	// 開始時に構築したバケットから取り出す
	const RenderPassItemList* list = passBuckets.Find(pass.queue);
	// 描画アイテムがないなら処理しない
	if (!list || list->IsEmpty()) {
		return;
	}

	// アイテムを描画
	std::string_view passName = pass.passName.empty() ? std::string_view("Draw") : std::string_view(pass.passName);
	dependencies.dispatcher->Dispatch(graphicsCore, context, *dependencies.renderBatch,
		*dependencies.backendRegistry, *dependencies.assetLibrary, *dependencies.pipelineCache,
		*dependencies.materialResolver, list->items, surface, passName, false);
}

void Engine::ScenePassExecutor::ExecutePostProcessPass(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const PostProcessPassDesc& pass) const {

	// レンダーターゲットを適用
	MultiRenderTarget* source = context.targetRegistry->Resolve(pass.source);
	MultiRenderTarget* dest = ApplyRenderTargets(graphicsCore, context, pass.dest);

	// フルスクリーンマテリアルを実行
	ExecuteFullscreenMaterial(graphicsCore, context, source, dest,
		pass.material, DefaultMaterialSlot::FullscreenCopy, "PostProcess");
}

void Engine::ScenePassExecutor::ExecuteComputePass(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const ComputePassDesc& pass) const {

	// 無効なマテリアルなら処理しない
	if (!pass.material) {
		return;
	}
	// マテリアルを読み込む
	const MaterialAsset* material = dependencies.assetLibrary->LoadMaterial(pass.material);
	if (!material) {
		return;
	}

	// パスを探す
	std::string_view passName = pass.passName.empty() ? std::string_view("Compute") : std::string_view(pass.passName);
	const MaterialPassBinding* passBinding = FindPass(*material, passName);
	if (!passBinding) {
		return;
	}
	// コンピュートパス以外は処理しない
	if (passBinding->preferredVariant != PipelineVariantKind::Compute) {
		return;
	}

	// パイプラインを取得
	const PipelineState* pipelineState = dependencies.pipelineCache->GetORCreate(graphicsCore.GetDXObject(),
		*dependencies.assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);

	// 書き込み先として使用するサーフェイスをレンダーターゲット用に遷移してバインドする
	MultiRenderTarget* source = context.targetRegistry->Resolve(pass.source);
	MultiRenderTarget* dest = context.targetRegistry->Resolve(pass.dest);

	// それぞれのシェーダーバインディングが存在するかどうかをチェック
	bool needSourceColor = (pipelineState->FindBinding(ShaderBindingKind::SRV, 0, 0) != nullptr);
	bool needSourceDepth = (pipelineState->FindBinding(ShaderBindingKind::SRV, 1, 0) != nullptr);
	bool needDestColorUAV = (pipelineState->FindBinding(ShaderBindingKind::UAV, 0, 0) != nullptr);
	// ディスパッチモードに応じた必要なサーフェイスのチェック
	bool needSourceForDispatch = (pass.dispatchMode == ComputeDispatchMode::FromSourceSize);
	bool needDestForDispatch = (pass.dispatchMode == ComputeDispatchMode::FromDestSize);

	// 必要なものだけ存在チェック
	if ((needSourceColor || needSourceDepth || needSourceForDispatch) && !source) {
		return;
	}
	if ((needDestColorUAV || needDestForDispatch) && !dest) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// 必要なリソースを適切な状態に遷移する
	if (needSourceColor) {
		RenderTexture2D* sourceColor = source->GetColorTexture(0);
		if (!sourceColor) {
			return;
		}
		sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	if (needSourceDepth) {
		auto* sourceDepth = source->GetDepthTexture();
		if (!sourceDepth) {
			return;
		}
		sourceDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
	if (needDestColorUAV) {
		RenderTexture2D* destColor = dest->GetColorTexture(0);
		if (!destColor) {
			return;
		}
		if (destColor->GetUAVGPUHandle().ptr == 0) {
			return;
		}
		destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// パイプラインを設定
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	// UAVバッファを収集
	std::vector<ID3D12Resource*> registryUAVResources =
		GatherRegistryUAVResources(context.bufferRegistry, *pipelineState);
	// UAV用に遷移
	if (!registryUAVResources.empty()) {

		dxCommand->TransitionBarriers(registryUAVResources,
			D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	}

	// ルートバインド設定構築
	ComputeRootBinder binder{ *pipelineState };
	std::vector<ComputeBindItem> bindItems{};
	// t0
	if (needSourceColor) {

		RenderTexture2D* sourceColor = source->GetColorTexture(0);
		bindItems.push_back({ {}, ComputeBindValueType::SRV, 0,sourceColor->GetSRVGPUHandle(),0, 0 });
	}
	// t1
	if (needSourceDepth) {

		DepthTexture2D* sourceDepth = source->GetDepthTexture();
		bindItems.push_back({ {}, ComputeBindValueType::SRV, 0,sourceDepth->GetSRVGPUHandle(),1, 0 });
	}
	// u0
	if (needDestColorUAV) {

		RenderTexture2D* destColor = dest->GetColorTexture(0);
		bindItems.push_back({ {}, ComputeBindValueType::UAV, 0,destColor->GetUAVGPUHandle(),0, 0 });
	}
	// レジストリに登録された名前付きバッファを自動バインド
	AppendComputeBufferBindings(context.bufferRegistry, *pipelineState, bindItems);
	binder.Bind(commandList, bindItems);

	// ディスパッチサイズを計算
	uint32_t dispatchX = pass.groupCountX;
	uint32_t dispatchY = pass.groupCountY;
	uint32_t dispatchZ = pass.groupCountZ;

	// ディスパッチモードに応じてディスパッチサイズを上書き
	switch (pass.dispatchMode) {
	case ComputeDispatchMode::Fixed:
		break;
	case ComputeDispatchMode::FromSourceSize:

		dispatchX = DxUtils::RoundUp(source->GetWidth(), pipelineState->GetThreadGroupX());
		dispatchY = DxUtils::RoundUp(source->GetHeight(), pipelineState->GetThreadGroupY());
		dispatchZ = 1;
		break;
	case ComputeDispatchMode::FromDestSize:

		dispatchX = DxUtils::RoundUp(dest->GetWidth(), pipelineState->GetThreadGroupX());
		dispatchY = DxUtils::RoundUp(dest->GetHeight(), pipelineState->GetThreadGroupY());
		dispatchZ = 1;
		break;
	}
	// 実行
	commandList->Dispatch(dispatchX, dispatchY, dispatchZ);

	// UAVバッファにUAVバリア
	for (ID3D12Resource* resource : registryUAVResources) {
		if (resource) {

			dxCommand->UAVBarrier(resource);
		}
	}

	// 次の描画で読むためSRV状態へ遷移
	if (!registryUAVResources.empty()) {

		dxCommand->TransitionBarriers(registryUAVResources, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
	}
}

void Engine::ScenePassExecutor::ExecuteBlitPass(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const BlitPassDesc& pass) const {

	// レンダーターゲットを適用
	MultiRenderTarget* source = context.targetRegistry->Resolve(pass.source);
	MultiRenderTarget* dest = ApplyRenderTargets(graphicsCore, context, pass.dest);

	// フルスクリーンマテリアルを実行
	ExecuteFullscreenMaterial(graphicsCore, context, source, dest,
		pass.material, DefaultMaterialSlot::FullscreenCopy, "Blit");
}

void Engine::ScenePassExecutor::ExecuteRaytracingPass(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const RaytracingPassDesc& pass) const {

	MultiRenderTarget* source = context.targetRegistry->Resolve(pass.source);
	MultiRenderTarget* dest = context.targetRegistry->Resolve(pass.dest);
	if (!source || !dest) {
		return;
	}

	// DispatchRaysを起動できないときのパススルー
	auto passthrough = [&]() {

		ExecuteFullscreenMaterial(graphicsCore, context, source, dest,
			AssetID{}, DefaultMaterialSlot::FullscreenCopy, "Blit");
		};

	if (!graphicsCore.GetDXObject().ShouldUseDispatchRays() ||
		!context.raytracing.tlasResource) {
		passthrough();
		return;
	}

	// マテリアルを取得
	const MaterialAsset* material = dependencies.assetLibrary->LoadMaterial(pass.material);
	if (!material) {
		passthrough();
		return;
	}
	// マテリアルパスを取得
	const MaterialPassBinding* passBinding = FindPass(*material, pass.passName);
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Raytracing) {
		passthrough();
		return;
	}

	// レイトレーシングパイプラインを取得
	RaytracingPipelineState* pipelineState = dependencies.raytracingPipelineCache->GetOrCreate(
		graphicsCore.GetDXObject(), *dependencies.assetLibrary, passBinding->pipeline);

	// 登録されている必要なバッファを取得
	const RegisteredRenderBuffer* tlas = context.bufferRegistry.Find("gSceneTLAS");
	const RegisteredRenderBuffer* viewConstants = context.bufferRegistry.Find("RaytracingViewConstants");
	const RegisteredRenderBuffer* sceneInstances = context.bufferRegistry.Find("gRaytracingSceneInstances");
	const RegisteredRenderBuffer* sceneSubMeshes = context.bufferRegistry.Find("gRaytracingSubMeshes");

	// 描画に使用するテクスチャを取得
	RenderTexture2D* sourceColor = source->GetColorTexture(0);
	DepthTexture2D* sourceDepth = source->GetDepthTexture();
	// 法線
	RenderTexture2D* sourceNormal = (1 < source->GetColorCount()) ? source->GetColorTexture(1) : nullptr;
	// ワールド座標
	RenderTexture2D* sourcePosition = (2 < source->GetColorCount()) ? source->GetColorTexture(2) : nullptr;
	// 書き込み先
	RenderTexture2D* destColor = dest->GetColorTexture(0);
	// 必要なリソースがなければ処理しないでパススルー
	if (!sourceColor || !sourceDepth || !sourceNormal || !sourcePosition || !destColor ||
		destColor->GetUAVGPUHandle().ptr == 0) {
		passthrough();
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// DispatchRaysに必要なリソースを使用できる状態に遷移する
	sourceColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourceNormal->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sourcePosition->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	destColor->Transition(*dxCommand, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// パイプライン設定
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState1(pipelineState->GetStateObject());

	// ルートパラメータ設定
	commandList->SetComputeRootShaderResourceView(RaytracingPipelineState::kRootIndexTLAS, tlas->gpuAddress);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceColor, sourceColor->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceDepth, sourceDepth->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourceNormal, sourceNormal->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSourcePosition, sourcePosition->GetSRVGPUHandle());
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSceneInstances, sceneInstances->srvGPUHandle);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexSceneSubMeshes, sceneSubMeshes->srvGPUHandle);
	commandList->SetComputeRootDescriptorTable(RaytracingPipelineState::kRootIndexDestUAV, destColor->GetUAVGPUHandle());
	commandList->SetComputeRootConstantBufferView(RaytracingPipelineState::kRootIndexViewCBV, viewConstants->gpuAddress);

	// レイデスク構築
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = pipelineState->BuildDispatchDesc(dest->GetWidth(), dest->GetHeight(), 1);

	// 実行
	commandList->DispatchRays(&dispatchDesc);

	// UAVバリアを入れて次の描画で読むためSRV状態へ遷移
	dxCommand->UAVBarrier(destColor->GetResource());
	destColor->Transition(*dxCommand, static_cast<D3D12_RESOURCE_STATES>(
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
}

void Engine::ScenePassExecutor::ExecuteRenderScenePass(GraphicsCore& graphicsCore,
	const RenderFrameRequest& request, const SceneExecutionContext& context,
	const RenderScenePassDesc& pass) {

	if (!context.sceneInstance || !request.sceneInstances) {
		return;
	}

	// サブシーンを見つける
	const SceneInstance* child = request.sceneInstances->FindChildBySlot(
		context.sceneInstance->instanceID, pass.subSceneSlot);
	if (!child) {
		return;
	}

	// レンダーターゲットを適用
	MultiRenderTarget* childSurface = ApplyRenderTargets(graphicsCore, context, pass.dest);
	if (!childSurface) {
		return;
	}

	// 子シーン用のコンテキストを構築
	SceneExecutionContext childContext{};
	childContext.kind = context.kind;
	childContext.sceneInstance = child;
	childContext.view = context.view;
	childContext.defaultSurface = childSurface;
	childContext.targetRegistry = context.targetRegistry;
	childContext.bufferRegistry = context.bufferRegistry;
	childContext.raytracing = context.raytracing;
	childContext.world = context.world;
	childContext.systemContext = context.systemContext;
	childContext.assetDatabase = context.assetDatabase;

	// 子シーンのレンダーターゲットを登録
	{
		std::string colorName = ViewportRenderService::GetPrimaryColorName(context.kind);
		std::optional<std::string> depthName = std::string(ViewportRenderService::GetPrimaryDepthName(context.kind));
		childContext.targetRegistry->Register("View", childSurface, { colorName }, depthName);
		childContext.targetRegistry->Register(ViewportRenderService::GetViewAlias(context.kind),
			childSurface, { colorName }, depthName);
	}

	for (const auto& renderTarget : child->header.renderTargets) {

		childContext.targetRegistry->ResizeTransient(graphicsCore,
			renderTarget, context.view->width, context.view->height);
	}
	// 構築した内容で子シーンを実行
	ExecuteScene(graphicsCore, request, childContext);
	// 結果を使用できるようにシェーダーリード用に遷移する
	TransitionAllTargetsToShaderRead(graphicsCore, childContext);
}

Engine::MultiRenderTarget* Engine::ScenePassExecutor::ApplyRenderTargets(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const RenderTargetSetReference& dest) const {

	// レンダーターゲットを解決する。解決できない場合はデフォルトサーフェイスを使用する
	MultiRenderTarget* surface = context.targetRegistry->Resolve(dest);
	if (!surface) {
		surface = context.defaultSurface;
	}
	if (!surface) {
		return nullptr;
	}
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	surface->TransitionForRender(*dxCommand);
	surface->Bind(*dxCommand);
	return surface;
}

std::vector<const Engine::RenderItem*> Engine::ScenePassExecutor::CollectDepthPrepassItems(
	const SceneExecutionContext& context, const DepthPrepassPassDesc& pass,
	const RenderPassPhaseBuckets& passBuckets) const {

	std::vector<const RenderItem*> result{};

	// シーン実行開始時に構築したバケットからアイテムを取得
	const RenderPassItemList* list = passBuckets.Find(pass.queue);
	if (!list || list->IsEmpty()) {
		return result;
	}

	// カメラを取得
	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		return result;
	}

	result.reserve(list->items.size());
	for (const RenderItem* item : list->items) {

		if (!item) {
			continue;
		}

		// メッシュ以外のアイテムは処理しない
		if (item->backendID != RenderBackendID::Mesh) {
			continue;
		}

		// カメラのカリングマスク判定
		if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}

		// 深度プリパスが有効なアイテムのみを対象とする
		const MeshRenderPayload* payload = dependencies.renderBatch->GetPayload<MeshRenderPayload>(*item);
		if (!payload || !payload->enableZPrepass) {
			continue;
		}
		result.emplace_back(item);
	}
	return result;
}

bool Engine::ScenePassExecutor::HasExplicitClearForDefaultSurface(const SceneExecutionContext& context, const SceneHeader& header) const {

	for (const auto& pass : header.passOrder) {

		if (pass.type != ScenePassType::Clear) {
			continue;
		}
		MultiRenderTarget* target = context.targetRegistry->Resolve(pass.clear.dest);
		if (!target) {
			target = context.defaultSurface;
		}
		if (target == context.defaultSurface) {
			return true;
		}
	}
	return false;
}
