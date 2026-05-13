#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/RenderViewResolver.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Graphics/Render/Light/Builtin/Directional/DirectionalLightExtractor.h>
#include <Engine/Core/Graphics/Render/Light/Builtin/Point/PointLightExtractor.h>
#include <Engine/Core/Graphics/Render/Light/Builtin/Spot/SpotLightExtractor.h>
#include <Engine/Core/Graphics/Render/Light/ViewLightCollector.h>
#include <Engine/Core/Graphics/Render/Queue/RenderPassItemCollector.h>
#include <Engine/Core/Graphics/Render/Pass/ScenePassExecutor.h>
#include <Engine/Core/Graphics/Line/LineRenderer.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/Scene/Instance/SceneInstanceManager.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Core/Graphics/Asset/MaterialAsset.h>
#include <Engine/Core/Graphics/Descriptors/SRVDescriptor.h>
#include <Engine/Core/Graphics/Pipeline/Bind/RootBindingCommandHelper.h>
#include <Engine/Logger/Assert.h>

// c++
#include <algorithm>

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================

namespace {

	uint32_t CalcHZBMipCount(uint32_t width, uint32_t height) {

		// 1x1になるまで落としたMip数を求める
		uint32_t mipCount = 1;
		while (width > 1 || height > 1) {

			width = (std::max)(1u, width >> 1);
			height = (std::max)(1u, height >> 1);
			++mipCount;
		}
		return mipCount;
	}

	uint32_t CalcMipSize(uint32_t value, uint32_t mip) {

		return (std::max)(1u, value >> mip);
	}

	void TransitionSubresource(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
		uint32_t subresource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {

		// 同じ状態への遷移はBarrierを発行しない
		if (!resource || before == after) {
			return;
		}
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = resource;
		barrier.Transition.Subresource = subresource;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		commandList->ResourceBarrier(1, &barrier);
	}

	void BindComputeCBV(ID3D12GraphicsCommandList* commandList, const Engine::PipelineState& pipeline,
		std::string_view name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) {

		// Pipeline側に存在するBindingだけを設定する
		if (gpuAddress == 0) {
			return;
		}
		if (const Engine::RootBindingLocation* binding = pipeline.FindBindingByName(name, Engine::ShaderBindingKind::CBV)) {

			Engine::RootBindingCommand::SetComputeCBV(commandList, binding, gpuAddress);
		}
	}

	void BindComputeSRV(ID3D12GraphicsCommandList* commandList, const Engine::PipelineState& pipeline,
		std::string_view name, D3D12_GPU_DESCRIPTOR_HANDLE descriptor) {

		// 使用しないShader VariantではBindingがない場合がある
		if (descriptor.ptr == 0) {
			return;
		}
		if (const Engine::RootBindingLocation* binding = pipeline.FindBindingByName(name, Engine::ShaderBindingKind::SRV)) {

			Engine::RootBindingCommand::SetComputeSRV(commandList, binding, 0, descriptor);
		}
	}

	void BindComputeUAV(ID3D12GraphicsCommandList* commandList, const Engine::PipelineState& pipeline,
		std::string_view name, D3D12_GPU_DESCRIPTOR_HANDLE descriptor) {

		// UAV未使用のVariantでも同じ呼び出し側を使えるようにする
		if (descriptor.ptr == 0) {
			return;
		}
		if (const Engine::RootBindingLocation* binding = pipeline.FindBindingByName(name, Engine::ShaderBindingKind::UAV)) {

			Engine::RootBindingCommand::SetComputeUAV(commandList, binding, 0, descriptor);
		}
	}

	// 深度前描画でスキニングする必要のあるメッシュ描画アイテムを収集する
	std::vector<const Engine::RenderItem*> CollectDepthPrepassMeshItemsForSkinning(const Engine::RenderSceneBatch& renderBatch,
		const Engine::SceneExecutionContext& context, const Engine::DepthPrepassPassDesc& pass,
		const Engine::RenderPassPhaseBuckets& passBuckets) {

		std::vector<const Engine::RenderItem*> result{};
		// 深度前描画のキューに属するアイテムを収集
		const Engine::RenderPassItemList* list = passBuckets.Find(pass.queue);
		if (!list || list->IsEmpty()) {
			return result;
		}
		// カメラを取得
		const Engine::ResolvedCameraView* camera = context.view->FindCamera(Engine::RenderCameraDomain::Perspective);
		if (!camera) {
			return result;
		}

		result.reserve(list->items.size());
		for (const Engine::RenderItem* item : list->items) {

			if (!item) {
				continue;
			}
			if (item->backendID != Engine::RenderBackendID::Mesh) {
				continue;
			}
			if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			const Engine::MeshRenderPayload* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(*item);
			// 深度描画対象のみ
			if (!payload || !payload->enableZPrepass) {
				continue;
			}
			result.emplace_back(item);
		}
		return result;
	}
	// スキニングする必要のあるメッシュ描画アイテムを収集して、バッチ処理する
	void PreDispatchVisibleMeshSkinning(Engine::GraphicsCore& graphicsCore, const Engine::SceneHeader& header,
		const Engine::SceneExecutionContext& context, const Engine::RenderSceneBatch& renderBatch,
		Engine::RenderBackendRegistry& backendRegistry, Engine::RenderAssetLibrary& assetLibrary,
		Engine::PipelineStateCache& pipelineCache, Engine::MaterialResolver& materialResolver,
		const Engine::RenderPassPhaseBuckets& passBuckets) {

		// メッシュ描画クラスを取得
		auto* baseBackend = backendRegistry.Find(Engine::RenderBackendID::Mesh);
		auto* meshBackend = dynamic_cast<Engine::MeshRenderBackend*>(baseBackend);
		if (!meshBackend || !context.view || !context.sceneInstance) {
			return;
		}

		// 描画コンテキストの構築
		Engine::RenderDrawContext drawContext{};
		drawContext.graphicsCore = &graphicsCore;
		drawContext.view = context.view;
		drawContext.cullingView = context.cullingView;
		drawContext.systemContext = context.systemContext;
		drawContext.batch = &renderBatch;
		drawContext.bufferRegistry = &context.bufferRegistry;
		drawContext.assetDatabase = context.assetDatabase;
		drawContext.assetLibrary = &assetLibrary;
		drawContext.pipelineCache = &pipelineCache;
		drawContext.materialResolver = &materialResolver;
		drawContext.forceVertexMeshVariant = context.forceVertexMeshVariant;

		// GPUのランタイム機能を取得。プレビューではRayQuery系Variantを選ばない。
		drawContext.runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
		if (context.disableInlineRayTracing) {

			drawContext.runtimeFeatures.useInlineRayTracing = false;
			drawContext.runtimeFeatures.useDispatchRays = false;
		}

		// バッチ処理のラムダ関数
		auto runDispatch = [&](const std::vector<const Engine::RenderItem*>& items) {

			size_t begin = 0;
			// アイテムをすべて処理するまでループ
			while (begin < items.size()) {

				const Engine::RenderItem* first = items[begin];
				// アイテムが有効で、メッシュ描画アイテムで、バッチ処理可能なものであるか
				if (!first || first->backendID != Engine::RenderBackendID::Mesh) {
					++begin;
					continue;
				}

				size_t end = begin + 1;
				while (end < items.size()) {
					const Engine::RenderItem* next = items[end];
					if (!next) {
						break;
					}
					// バッチ処理可能かどうかを判定
					if (next->backendID != first->backendID ||
						!meshBackend->CanBatch(*first, *next, drawContext.runtimeFeatures)) {
						break;
					}
					++end;
				}

				// スキニング実行
				meshBackend->PreDispatchSkinningBatch(drawContext, std::span(items.data() + begin, end - begin));

				// 次のバッチ処理の開始位置を更新
				begin = end;
			}
			};

		for (const auto& pass : header.passOrder) {
			switch (pass.type) {
			case Engine::ScenePassType::DepthPrepass: {

				runDispatch(CollectDepthPrepassMeshItemsForSkinning(renderBatch, context, pass.depthPrepass, passBuckets));
				break;
			}
			case Engine::ScenePassType::Draw: {

				const Engine::RenderPassItemList* list = passBuckets.Find(pass.draw.queue);
				if (list && !list->IsEmpty()) {

					runDispatch(list->items);
				}
				break;
			}
			}
		}
	}
	// ビューに対して可視なメッシュアセットIDを収集する
	void CollectVisibleMeshAssetsForView(const Engine::RenderSceneBatch& renderBatch,
		Engine::UUID sceneInstanceID, const Engine::ResolvedRenderView& view,
		std::unordered_set<Engine::AssetID>& outMeshAssets) {

		if (!view.valid) {
			return;
		}
		for (const Engine::RenderItem& item : renderBatch.GetItems()) {

			if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
				continue;
			}
			if (item.backendID != Engine::RenderBackendID::Mesh) {
				continue;
			}

			const Engine::ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
			if (!camera || (item.visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			const Engine::MeshRenderPayload* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(item);
			if (!payload || !payload->mesh) {
				continue;
			}
			// メッシュアセットIDを収集
			outMeshAssets.insert(payload->mesh);
		}
	}
	// 指定Entityがrootの子階層に含まれているか確認する
	bool IsEntityInPreviewTree(Engine::ECSWorld& world, Engine::Entity root, Engine::Entity entity) {

		if (!world.IsAlive(root) || !world.IsAlive(entity)) {
			return false;
		}
		if (root == entity) {
			return true;
		}

		Engine::Entity current = entity;
		while (const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(current)) {

			if (!world.IsAlive(hierarchy->parent)) {
				break;
			}
			if (hierarchy->parent == root) {
				return true;
			}
			current = hierarchy->parent;
		}
		return false;
	}
	// Entityが所属するシーンインスタンスIDを取得する
	Engine::UUID ResolveEntitySceneInstanceID(Engine::ECSWorld& world, Engine::Entity entity, Engine::UUID fallback) {

		if (const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(entity)) {
			if (sceneObject->sceneInstanceID) {
				return sceneObject->sceneInstanceID;
			}
		}
		return fallback;
	}
	// プレビューではポストエフェクトを通さず、描画パスだけを専用サーフェイスへ流す
	Engine::SceneHeader BuildPreviewSceneHeader(const Engine::SceneHeader* source,
		const Engine::Color4& clearColor, bool clearSurface) {

		Engine::SceneHeader header{};
		if (source) {
			header = *source;
		}
		header.renderTargets.clear();
		header.subScenes.clear();
		header.passOrder.clear();

		Engine::ScenePassDesc clearPass{};
		clearPass.type = Engine::ScenePassType::Clear;
		clearPass.clear.clearColor = clearSurface;
		clearPass.clear.clearColorValue = clearColor;
		clearPass.clear.clearDepth = clearSurface;
		clearPass.clear.clearDepthValue = 1.0f;
		clearPass.clear.clearStencil = false;
		header.passOrder.emplace_back(clearPass);

		if (source) {
			for (const Engine::ScenePassDesc& pass : source->passOrder) {

				if (pass.type == Engine::ScenePassType::DepthPrepass) {

					Engine::ScenePassDesc previewPass = pass;
					previewPass.depthPrepass.dest = {};
					header.passOrder.emplace_back(std::move(previewPass));
					continue;
				}
				if (pass.type == Engine::ScenePassType::Draw) {

					Engine::ScenePassDesc previewPass = pass;
					previewPass.draw.dest = {};
					header.passOrder.emplace_back(std::move(previewPass));
				}
			}
		}

		bool hasDrawPass = false;
		for (const Engine::ScenePassDesc& pass : header.passOrder) {
			if (pass.type == Engine::ScenePassType::Draw) {
				hasDrawPass = true;
				break;
			}
		}
		if (!hasDrawPass) {

			for (const char* queue : { "CanvasPreModel", "Opaque", "Transparent" }) {

				Engine::ScenePassDesc drawPass{};
				drawPass.type = Engine::ScenePassType::Draw;
				drawPass.draw.queue = queue;
				drawPass.draw.passName = "Draw";
				header.passOrder.emplace_back(std::move(drawPass));
			}
		}
		return header;
	}
	// 指定キューのDrawPassがない場合だけ追加する
	void EnsurePreviewDrawPass(Engine::SceneHeader& header, const std::string& queue) {

		for (const Engine::ScenePassDesc& pass : header.passOrder) {
			if (pass.type == Engine::ScenePassType::Draw && pass.draw.queue == queue) {
				return;
			}
		}

		Engine::ScenePassDesc drawPass{};
		drawPass.type = Engine::ScenePassType::Draw;
		drawPass.draw.queue = queue;
		drawPass.draw.passName = "Draw";
		header.passOrder.emplace_back(std::move(drawPass));
	}
	// プレビュー対象の描画アイテムだけを描画フェーズごとに振り分ける
	void BuildPreviewPassBuckets(Engine::ECSWorld& world, Engine::Entity root,
		const Engine::RenderSceneBatch& renderBatch, const Engine::ResolvedRenderView& view,
		Engine::RenderPassPhaseBuckets& outBuckets, std::vector<Engine::AssetID>& outMeshAssets) {

		outBuckets.Clear();
		outMeshAssets.clear();

		std::unordered_set<uint64_t> meshAssetSet{};
		for (const Engine::RenderItem& item : renderBatch.GetItems()) {

			if (!IsEntityInPreviewTree(world, root, item.entity)) {
				continue;
			}

			const Engine::ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
			if (!camera || (item.visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			outBuckets.phaseToItems[item.renderPhase].items.emplace_back(&item);
			if (item.backendID == Engine::RenderBackendID::Mesh) {

				if (const auto* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(item)) {
					if (payload->mesh) {
						meshAssetSet.emplace(payload->mesh.value);
					}
				}
			}
		}

		outMeshAssets.reserve(meshAssetSet.size());
		for (uint64_t meshValue : meshAssetSet) {
			outMeshAssets.emplace_back(Engine::AssetID{ meshValue });
		}
	}
}

void Engine::HierarchicalZBuffer::EnsureSize(GraphicsCore& graphicsCore, uint32_t width, uint32_t height) {

	// 0サイズのViewでもGPUリソースを作れるよう最低1に丸める
	width = (std::max)(1u, width);
	height = (std::max)(1u, height);
	const uint32_t mipCount = CalcHZBMipCount(width, height);
	// サイズが変わっていなければ前回のDescriptor/Resourceを再利用する
	if (resource_ && width_ == width && height_ == height && mipCount_ == mipCount) {
		return;
	}

	// Viewサイズ変更時はMip数も変わるためDescriptorごと作り直す
	Release(&graphicsCore.GetSRVDescriptor());

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor& srvDescriptor = graphicsCore.GetSRVDescriptor();
	srvDescriptor_ = &srvDescriptor;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = static_cast<UINT16>(mipCount);
	resourceDesc.Format = DXGI_FORMAT_R32_FLOAT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	// ComputeでMipを書き込むためUAV付きのR32_FLOAT Textureとして確保する
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties, D3D12_HEAP_FLAG_NONE, &resourceDesc,
		D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&resource_));
	Assert::Call(SUCCEEDED(hr), "Create HZB resource failed");
	resource_->SetName(L"GameView.HZB");

	width_ = width;
	height_ = height;
	mipCount_ = mipCount;
	built_ = false;

	if (!buildConstants_.IsCreatedResource()) {
		buildConstants_.CreateBuffer(device);
	}
	if (!cullingConstants_.IsCreatedResource()) {
		cullingConstants_.CreateBuffer(device);
	}

	// 描画カリング側はTexture全体をSRVとして参照する
	D3D12_SHADER_RESOURCE_VIEW_DESC fullSRVDesc{};
	fullSRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
	fullSRVDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	fullSRVDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	fullSRVDesc.Texture2D.MipLevels = mipCount_;
	srvDescriptor.CreateSRV(fullSRVIndex_, resource_.Get(), fullSRVDesc);
	fullSRVHandle_ = srvDescriptor.GetGPUHandle(fullSRVIndex_);

	mipSRVHandles_.resize(mipCount_);
	mipUAVHandles_.resize(mipCount_);
	mipSRVIndices_.assign(mipCount_, UINT32_MAX);
	mipUAVIndices_.assign(mipCount_, UINT32_MAX);
	mipStates_.assign(mipCount_, D3D12_RESOURCE_STATE_COMMON);
	for (uint32_t mip = 0; mip < mipCount_; ++mip) {

		// 各Mipを前段の読み込み元として使うため、Mip単位SRVを作る
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = mip;
		srvDesc.Texture2D.MipLevels = 1;
		srvDescriptor.CreateSRV(mipSRVIndices_[mip], resource_.Get(), srvDesc);
		mipSRVHandles_[mip] = srvDescriptor.GetGPUHandle(mipSRVIndices_[mip]);

		// 各MipをComputeの書き込み先にするため、Mip単位UAVを作る
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
		uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = mip;
		srvDescriptor.CreateUAV(mipUAVIndices_[mip], resource_.Get(), uavDesc);
		mipUAVHandles_[mip] = srvDescriptor.GetGPUHandle(mipUAVIndices_[mip]);
	}

	UploadCullingConstants();
}

void Engine::HierarchicalZBuffer::Build(GraphicsCore& graphicsCore, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, AssetDatabase& assetDatabase, DepthTexture2D* depthTexture) {

	if (!depthTexture || !depthTexture->IsValid()) {
		return;
	}
	// 深度Textureのサイズに合わせてHZBを用意する
	EnsureSize(graphicsCore, depthTexture->GetWidth(), depthTexture->GetHeight());
	if (!resource_) {
		return;
	}

	if (!buildPipeline_) {
		buildPipeline_ = assetDatabase.ImportOrGet(
			"Engine/Assets/Pipelines/Builtin/HZB/buildHZB.pipeline.json", AssetType::RenderPipeline);
	}
	const PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
		assetLibrary, buildPipeline_, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState) {
		return;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	// ZPrepassのDepthをComputeで読める状態にする
	depthTexture->Transition(*dxCommand, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	for (uint32_t mip = 0; mip < mipCount_; ++mip) {

		// 生成先MipだけUAV状態へ遷移する
		TransitionSubresource(commandList, resource_.Get(), mip,
			mipStates_[mip], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mipStates_[mip] = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

		HZBBuildConstants constants{};
		// Mip0はDepthTexture、Mip1以降は直前のHZB Mipを読む
		constants.sourceWidth = (mip == 0) ? depthTexture->GetWidth() : CalcMipSize(width_, mip - 1);
		constants.sourceHeight = (mip == 0) ? depthTexture->GetHeight() : CalcMipSize(height_, mip - 1);
		constants.destWidth = CalcMipSize(width_, mip);
		constants.destHeight = CalcMipSize(height_, mip);
		buildConstants_.TransferData(constants);

		BindComputeCBV(commandList, *pipelineState, "HZBBuildConstants", buildConstants_.GetResource()->GetGPUVirtualAddress());
		BindComputeSRV(commandList, *pipelineState, "gSourceDepth",
			(mip == 0) ? depthTexture->GetSRVGPUHandle() : mipSRVHandles_[mip - 1]);
		BindComputeUAV(commandList, *pipelineState, "gDestHZB", mipUAVHandles_[mip]);

		// 8x8スレッド単位で現在のMipを生成する
		commandList->Dispatch(DxUtils::RoundUp(constants.destWidth, pipelineState->GetThreadGroupX()),
			DxUtils::RoundUp(constants.destHeight, pipelineState->GetThreadGroupY()), 1);

		// 次のMipがこのMipを読むため、書き込み完了を保証してSRV状態へ戻す
		dxCommand->UAVBarrier(resource_.Get());
		TransitionSubresource(commandList, resource_.Get(), mip,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		mipStates_[mip] = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	}

	built_ = true;
	UploadCullingConstants();
}

void Engine::HierarchicalZBuffer::RegisterTo(RenderBufferRegistry& registry, bool occlusionEnabled) {

	// Build前やメニューOFF時はocclusionEnabledを0にして、シェーダ側を安全側に倒す
	lastOcclusionEnabled_ = occlusionEnabled;
	UploadCullingConstants();
	if (!resource_) {
		return;
	}

	registry.Register({ .alias = "HZBConstants",.resource = nullptr,
		.gpuAddress = cullingConstants_.GetResource()->GetGPUVirtualAddress(),.srvGPUHandle = {},.uavGPUHandle = {},
		.elementCount = 1,.stride = sizeof(HZBCullingConstants) });
	// HZB本体は全Mipを参照できるSRVとして登録する
	registry.Register({ .alias = "gHZBTexture",.resource = resource_.Get(),
		.gpuAddress = 0,.srvGPUHandle = fullSRVHandle_,.uavGPUHandle = {},
		.elementCount = mipCount_,.stride = 0 });
}

void Engine::HierarchicalZBuffer::Invalidate() {

	// GameViewのZPrepass前に前フレームのHZBを使わないよう無効化する
	built_ = false;
	UploadCullingConstants();
}

void Engine::HierarchicalZBuffer::Release(SRVDescriptor* srvDescriptor) {

	// 呼び出し側からDescriptorが渡されない場合は作成時に保持したDescriptorを使う
	if (!srvDescriptor) {
		srvDescriptor = srvDescriptor_;
	}
	if (srvDescriptor) {
		if (fullSRVIndex_ != UINT32_MAX) {
			srvDescriptor->Free(fullSRVIndex_);
		}
		for (uint32_t index : mipSRVIndices_) {
			if (index != UINT32_MAX) {
				srvDescriptor->Free(index);
			}
		}
		for (uint32_t index : mipUAVIndices_) {
			if (index != UINT32_MAX) {
				srvDescriptor->Free(index);
			}
		}
	}

	resource_.Reset();
	mipSRVHandles_.clear();
	mipUAVHandles_.clear();
	mipSRVIndices_.clear();
	mipUAVIndices_.clear();
	mipStates_.clear();
	fullSRVIndex_ = UINT32_MAX;
	fullSRVHandle_ = {};
	width_ = 0;
	height_ = 0;
	mipCount_ = 0;
	srvDescriptor_ = nullptr;
	built_ = false;
}

void Engine::HierarchicalZBuffer::UploadCullingConstants() {

	if (!cullingConstants_.IsCreatedResource()) {
		return;
	}

	// HZB未生成のフレームでは描画側でOcclusion判定を行わない
	HZBCullingConstants constants{};
	constants.width = (std::max)(1u, width_);
	constants.height = (std::max)(1u, height_);
	constants.mipCount = (std::max)(1u, mipCount_);
	constants.occlusionEnabled = (lastOcclusionEnabled_ && built_) ? 1u : 0u;
	constants.occlusionBias = 0.0005f;
	cullingConstants_.TransferData(constants);
}

void Engine::RenderPipelineRunner::Init() {

	viewportRenderService_ = std::make_unique<ViewportRenderService>();

	// 描画アイテム抽出器の登録
	extractorRegistry_.Clear();
	extractorRegistry_.Register(std::make_unique<SpriteRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<TextRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<MeshRenderItemExtractor>());
	// 描画バックエンドの登録
	backendRegistry_.Clear();
	backendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	backendRegistry_.Register(std::make_unique<TextRenderBackend>());
	backendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	// ツールプレビューはメインビューとは別のGPUバッファを持たせる
	previewBackendRegistry_.Clear();
	previewBackendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<TextRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	// ライト抽出器の登録
	lightExtractorRegistry_.Clear();
	lightExtractorRegistry_.Register(std::make_unique<DirectionalLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<PointLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<SpotLightExtractor>());

	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	frameLightBatch_.Clear();
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	previewLightSet_.Clear();

	raytracingPipelineStateCache_.Clear();
	gameViewRaytracingBuffers_.Release();
	sceneViewRaytracingBuffers_.Release();

	// ビューライトバッファの初期化
	gameViewLightBuffers_.Release();
	sceneViewLightBuffers_.Release();
	gameViewLightCullingBuffers_.Release();
	sceneViewLightCullingBuffers_.Release();
	previewLightBufferPool_.Clear();
	previewLightCullingBufferPool_.Clear();
	previewBackendFrameStarted_ = false;
}

void Engine::RenderPipelineRunner::Finalize() {

	backendRegistry_.Clear();
	previewBackendRegistry_.Clear();
	extractorRegistry_.Clear();
	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	lightExtractorRegistry_.Clear();
	frameLightBatch_.Clear();
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	previewLightSet_.Clear();
	gameViewLightBuffers_.Release();
	sceneViewLightBuffers_.Release();
	gameViewLightCullingBuffers_.Release();
	sceneViewLightCullingBuffers_.Release();
	previewLightBufferPool_.Clear();
	previewLightCullingBufferPool_.Clear();
	viewportRenderService_.reset();
	raytracingPipelineStateCache_.Clear();
	gameViewRaytracingBuffers_.Release();
	sceneViewRaytracingBuffers_.Release();
	previewBackendFrameStarted_ = false;
	raytracingSceneBuilder_.Finalize();
	gameViewHZB_.Release();
}

void Engine::RenderPipelineRunner::Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	// エディタPostSceneで複数のプレビューを描画するため、プレビューbackendのリソースプールは
	// ここでフレーム境界だけリセットし、RenderEntityPreviewごとにはリセットしない。
	previewBackendFrameStarted_ = false;

	// ワールドがない場合は描画できないので処理しない
	if (!request.world) {
		return;
	}

	// データクリア
	sceneViewTLASResource_ = nullptr;
	sceneViewPickRecords_.clear();

	// アセットライブラリの初期化、フレーム開始処理
	renderAssetLibrary_.Init(request.assetDatabase);
	backendRegistry_.BeginFrame(graphicsCore);

	// レイトレシーンフレーム開始処理
	raytracingSceneBuilder_.BeginFrame(graphicsCore);

	// 描画要求に基づいて必要なサーフェイスをGPUと同期し、ビュー情報を決定
	SyncRequestedSurfaces(graphicsCore, request);
	ResolveViews(request);

	// 描画アイテムの抽出
	extractorRegistry_.BuildBatch(*request.world, renderBatch_);
	// ライト抽出
	lightExtractorRegistry_.BuildBatch(*request.world, frameLightBatch_);

	// アクティブなシーンインスタンスの取得
	const SceneInstance* activeScene = nullptr;
	if (request.sceneInstances) {
		activeScene = request.sceneInstances->Find(request.activeSceneInstanceID);
		if (!activeScene) {
			activeScene = request.sceneInstances->GetActive();
		}
	}

	// メッシュ描画クラスの取得
	auto* meshBackendBase = backendRegistry_.Find(RenderBackendID::Mesh);
	auto* meshBackend = dynamic_cast<MeshRenderBackend*>(meshBackendBase);

	std::unordered_set<AssetID> visibleMeshSet{};
	visibleMeshSet.reserve(renderBatch_.GetItems().size());

	// ビューごとに可視なメッシュアセットIDを収集
	if (meshBackend && activeScene) {
		if (gameView_.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, gameView_, visibleMeshSet);
		}
		if (sceneView_.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, sceneView_, visibleMeshSet);
		}
	}

	std::vector<AssetID> visibleMeshes{};
	visibleMeshes.reserve(visibleMeshSet.size());
	for (const AssetID& id : visibleMeshSet) {
		visibleMeshes.emplace_back(id);
	}
	// GPUに可視なメッシュの情報を要求して、必要なリソースを準備
	if (meshBackend && !visibleMeshes.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, visibleMeshes);
	}

	// ビューごとのライト集合クリア
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	// ルートシーン用のビューライト構築
	if (activeScene) {
		if (gameView_.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, gameView_, gameViewLightSet_);
		}
		if (sceneView_.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, sceneView_, sceneViewLightSet_);
		}
	}

	// GPUライトバッファ初期化
	if (!gameViewLightBuffers_.IsInitialized()) {
		gameViewLightBuffers_.Init(graphicsCore);
	}
	if (!sceneViewLightBuffers_.IsInitialized()) {
		sceneViewLightBuffers_.Init(graphicsCore);
	}
	if (!gameViewLightCullingBuffers_.IsInitialized()) {
		gameViewLightCullingBuffers_.Init(graphicsCore);
	}
	if (!sceneViewLightCullingBuffers_.IsInitialized()) {
		sceneViewLightCullingBuffers_.Init(graphicsCore);
	}
	// ビューごとのライト集合をGPUへ転送
	gameViewLightBuffers_.Upload(gameViewLightSet_);
	sceneViewLightBuffers_.Upload(sceneViewLightSet_);
	// ビューごとのライトカリングデータをGPUへ転送
	gameViewLightCullingBuffers_.Upload(gameView_, gameViewLightSet_);
	sceneViewLightCullingBuffers_.Upload(sceneView_, sceneViewLightSet_);

	// レイトレーシングビュー関連バッファの初期化と転送
	if (!gameViewRaytracingBuffers_.IsInitialized()) {
		gameViewRaytracingBuffers_.Init(graphicsCore);
	}
	if (!sceneViewRaytracingBuffers_.IsInitialized()) {
		sceneViewRaytracingBuffers_.Init(graphicsCore);
	}
	gameViewRaytracingBuffers_.Upload(gameView_);
	sceneViewRaytracingBuffers_.Upload(sceneView_);

	// シーン描画の実行
	ScenePassExecutor::Dependencies deps{};
	deps.renderBatch = &renderBatch_;
	deps.backendRegistry = &backendRegistry_;
	deps.assetLibrary = &renderAssetLibrary_;
	deps.pipelineCache = &pipelineStateCache_;
	deps.materialResolver = &materialResolver_;
	deps.viewportService = viewportRenderService_.get();
	deps.dispatcher = &batchDispatcher_;
	deps.raytracingPipelineCache = &raytracingPipelineStateCache_;
	ScenePassExecutor executor{ deps };

	// 描画ビューごとに描画を実行
	auto renderView = [&](RenderViewKind kind, const ResolvedRenderView& view) {
		if (!view.valid) {
			return;
		}

		SceneExecutionContext context = BuildViewExecutionContext(graphicsCore, request, activeScene, kind, view);
		if (!context.sceneInstance) {
			return;
		}
		if (kind == RenderViewKind::Game && context.hzbBuildTarget) {

			// GameViewのDepthPrepassで作り直すまでHZBカリングを無効にしておく
			context.hzbBuildTarget->Invalidate();
		}

		RenderPassPhaseBuckets passBuckets{};
		RenderPassItemCollector::BuildBucketsForViewAndScene(
			renderBatch_, view, context.sceneInstance->instanceID, passBuckets);

		// スキニングメッシュの頂点更新
		if (meshBackend) {
			PreDispatchVisibleMeshSkinning(graphicsCore, context.sceneInstance->header, context,
				renderBatch_, backendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets);

			// レイトレーシングシーンの構築
			raytracingSceneBuilder_.BuildForScene(graphicsCore, *request.assetDatabase, meshBackend, renderBatch_, context);
		}

		// TLASリソースとピック用のサブメッシュ情報を取得して保存
		if (kind == RenderViewKind::Scene) {

			sceneViewTLASResource_ = context.raytracing.tlasResource;
			sceneViewPickRecords_ = raytracingSceneBuilder_.GetPickRecords();
		}

		// TLASバッファをリソースレジストリに登録
		if (context.raytracing.tlasResource) {

			context.bufferRegistry.Register({ .alias = "SceneTLAS",.resource = context.raytracing.tlasResource,
				.gpuAddress = context.raytracing.tlasResource->GetGPUVirtualAddress(),.srvGPUHandle = {},.uavGPUHandle = {},
				.elementCount = context.raytracing.instanceCount,.stride = 0 });
			context.bufferRegistry.Register({ .alias = "gSceneTLAS",.resource = context.raytracing.tlasResource,
				.gpuAddress = context.raytracing.tlasResource->GetGPUVirtualAddress(),.srvGPUHandle = {},.uavGPUHandle = {},
				.elementCount = context.raytracing.instanceCount,.stride = 0 });
		}

		// シーンの実行
		executor.ExecuteScene(graphicsCore, request, context, &passBuckets);

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (kind == RenderViewKind::Scene && context.defaultSurface) {

			LineRenderer::GetInstance()->RenderSceneView(graphicsCore, view, *context.defaultSurface);
		}
#endif

		executor.TransitionAllTargetsToShaderRead(graphicsCore, context);
		};
	renderView(RenderViewKind::Game, gameView_);
	renderView(RenderViewKind::Scene, sceneView_);
}

bool Engine::RenderPipelineRunner::PresentViewToBackBuffer(
	GraphicsCore& graphicsCore, RenderViewKind kind, AssetID material) {

	// 指定された種類の描画ビューのサーフェスを取得
	MultiRenderTarget* source = viewportRenderService_->GetSurface(kind);
	AssetDatabase* assetDatabase = renderAssetLibrary_.GetDatabase();
	if (!source || !source->GetColorTexture(0) || !assetDatabase) {
		return false;
	}

	// フルスクリーンコピー用のマテリアルを取得して読み込む
	AssetID resolvedMaterialID = materialResolver_.ResolveORDefault(*assetDatabase, material, DefaultMaterialSlot::FullscreenCopy);
	const MaterialAsset* materialAsset = renderAssetLibrary_.LoadMaterial(resolvedMaterialID);
	if (!materialAsset) {
		return false;
	}

	// ブリットパスかフルスクリーンパスを探す
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, "Blit");
	if (!passBinding) {
		passBinding = FindPass(*materialAsset, "Fullscreen");
	}
	// 無効なパスは処理しない
	if (passBinding->preferredVariant == PipelineVariantKind::Compute ||
		passBinding->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	// バックバッファのフォーマットに合わせたパイプラインステートを取得
	std::vector<DXGI_FORMAT> rtvFormats = {
		graphicsCore.GetSwapChainDesc().Format
	};
	const PipelineState* pipelineState = pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(),
		renderAssetLibrary_, passBinding->pipeline, passBinding->preferredVariant, rtvFormats, DXGI_FORMAT_UNKNOWN);

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// ソースをシェーダーリード状態に遷移
	source->TransitionForShaderRead(*dxCommand);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// パイプラインを設定
	commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(BlendMode::Normal));

	// サーフェイスを設定
	if (const RootBindingLocation* sourceColorBinding = pipelineState->FindBinding(ShaderBindingKind::SRV, 0, 0)) {

		commandList->SetGraphicsRootDescriptorTable(sourceColorBinding->rootParameterIndex, source->GetColorTexture(0)->GetSRVGPUHandle());
	}

	// バックバッファをレンダーターゲットとしてバインド
	dxCommand->BindRenderTargets(std::optional<RenderTarget>(graphicsCore.GetBackBufferRenderTarget()), std::nullopt);
	dxCommand->SetViewportAndScissor(static_cast<uint32_t>(graphicsCore.GetSwapChainDesc().Width), static_cast<uint32_t>(graphicsCore.GetSwapChainDesc().Height));

	// 全画面三角形を描画
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);

	return true;
}

bool Engine::RenderPipelineRunner::RenderEntityPreview(
	GraphicsCore& graphicsCore, const EntityPreviewRenderRequest& request) {

	if (!request.world || !request.assetDatabase || !request.surface || !request.surface->IsValid() ||
		!request.world->IsAlive(request.rootEntity)) {
		return false;
	}

	// メインのScene/Gameとは別に、ツール用サーフェイスだけを描画対象にする。
	renderAssetLibrary_.Init(request.assetDatabase);
	if (!previewBackendFrameStarted_) {

		previewBackendRegistry_.BeginFrame(graphicsCore);
		previewLightBufferPool_.BeginFrame();
		previewLightCullingBufferPool_.BeginFrame();
		previewBackendFrameStarted_ = true;
	}
	extractorRegistry_.BuildBatch(*request.world, renderBatch_);
	lightExtractorRegistry_.BuildBatch(*request.world, frameLightBatch_);

	RenderViewRequest viewRequest{};
	viewRequest.kind = RenderViewKind::Scene;
	viewRequest.enabled = true;
	viewRequest.width = request.useViewportRect && request.viewportWidth > 0 ? request.viewportWidth : request.surface->GetWidth();
	viewRequest.height = request.useViewportRect && request.viewportHeight > 0 ? request.viewportHeight : request.surface->GetHeight();
	viewRequest.sourceKind = RenderViewSourceKind::ManualCamera;
	viewRequest.manualCamera = request.camera;
	ResolvedRenderView previewView = RenderViewResolver::Resolve(viewRequest, *request.world);
	if (!previewView.valid) {
		return false;
	}

	SceneInstance previewScene{};
	previewScene.instanceID = ResolveEntitySceneInstanceID(*request.world, request.rootEntity, request.sceneInstanceID);
	previewScene.header = BuildPreviewSceneHeader(request.sceneHeader, request.clearColor, request.clearSurface);

	RenderPassPhaseBuckets passBuckets{};
	std::vector<AssetID> meshAssets{};
	BuildPreviewPassBuckets(*request.world, request.rootEntity, renderBatch_, previewView, passBuckets, meshAssets);
	for (const auto& [queue, list] : passBuckets.phaseToItems) {
		if (!list.IsEmpty()) {

			EnsurePreviewDrawPass(previewScene.header, queue);
		}
	}

	RenderTargetRegistry previewTargetRegistry{};
	previewTargetRegistry.BeginFrame();
	previewTargetRegistry.Register("View", request.surface, { "Preview.Color" }, std::string("Preview.Depth"));
	previewTargetRegistry.Register(ViewportRenderService::GetViewAlias(RenderViewKind::Scene),
		request.surface, { "Preview.Color" }, std::string("Preview.Depth"));

	SceneExecutionContext context{};
	context.kind = RenderViewKind::Scene;
	context.sceneInstance = &previewScene;
	context.view = &previewView;
	context.defaultSurface = request.surface;
	context.targetRegistry = &previewTargetRegistry;
	context.useViewportRect = request.useViewportRect;
	context.viewportX = request.viewportX;
	context.viewportY = request.viewportY;
	context.viewportWidth = request.viewportWidth;
	context.viewportHeight = request.viewportHeight;
	context.disableInlineRayTracing = true;
	context.forceVertexMeshVariant = request.forceVertexMeshVariant;
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;

	// プレビュー用のライトバッファを更新する。SceneView/GameViewのGPUバッファは触らない。
	previewLightSet_.Clear();
	ViewLightCollector::CollectForView(frameLightBatch_, &previewScene, previewView, previewLightSet_);
	ViewLightBufferSet& previewLightBuffers = previewLightBufferPool_.Acquire(graphicsCore,
		[](ViewLightBufferSet& buffers, GraphicsCore& core) {
			buffers.Init(core);
		});
	ViewLightCullingBufferSet& previewLightCullingBuffers = previewLightCullingBufferPool_.Acquire(graphicsCore,
		[](ViewLightCullingBufferSet& buffers, GraphicsCore& core) {
			buffers.Init(core);
		});
	previewLightBuffers.Upload(previewLightSet_);
	previewLightCullingBuffers.Upload(previewView, previewLightSet_);
	previewLightBuffers.RegisterTo(context.bufferRegistry);
	previewLightCullingBuffers.RegisterTo(context.bufferRegistry);

	auto* meshBackendBase = previewBackendRegistry_.Find(RenderBackendID::Mesh);
	auto* meshBackend = dynamic_cast<MeshRenderBackend*>(meshBackendBase);
	if (meshBackend && !meshAssets.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, meshAssets);
		PreDispatchVisibleMeshSkinning(graphicsCore, previewScene.header, context,
			renderBatch_, previewBackendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets);
	}

	ScenePassExecutor::Dependencies deps{};
	deps.renderBatch = &renderBatch_;
	deps.backendRegistry = &previewBackendRegistry_;
	deps.assetLibrary = &renderAssetLibrary_;
	deps.pipelineCache = &pipelineStateCache_;
	deps.materialResolver = &materialResolver_;
	deps.viewportService = viewportRenderService_.get();
	deps.dispatcher = &batchDispatcher_;
	deps.raytracingPipelineCache = &raytracingPipelineStateCache_;
	ScenePassExecutor executor{ deps };
	executor.ExecuteScene(graphicsCore, RenderFrameRequest{}, context, &passBuckets);

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (request.drawGrid2D) {

		LineRenderer::GetInstance()->Get2D()->DrawGrid();
	}
	if (request.drawGrid3D) {

		LineRenderer::GetInstance()->Get3D()->DrawGrid();
	}
	LineRenderer::GetInstance()->RenderSceneView(graphicsCore, previewView, *request.surface, false, false);
#endif

	executor.TransitionAllTargetsToShaderRead(graphicsCore, context);

	return true;
}

void Engine::RenderPipelineRunner::SyncRequestedSurfaces(
	GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	for (const auto& viewRequest : request.views) {
		if (!viewRequest.enabled) {
			continue;
		}
		viewportRenderService_->SyncSurface(graphicsCore, viewRequest.kind, viewRequest.width, viewRequest.height);
	}
}

void Engine::RenderPipelineRunner::ResolveViews(const RenderFrameRequest& request) {

	gameView_ = {};
	sceneView_ = {};
	for (const auto& viewRequest : request.views) {

		ResolvedRenderView resolved = RenderViewResolver::Resolve(viewRequest, *request.world);
		switch (viewRequest.kind) {
		case RenderViewKind::Game:

			gameView_ = resolved;
			break;
		case RenderViewKind::Scene:

			sceneView_ = resolved;
			break;
		}
	}
}

Engine::SceneExecutionContext Engine::RenderPipelineRunner::BuildViewExecutionContext(GraphicsCore& graphicsCore,
	const RenderFrameRequest& request, const SceneInstance* sceneInstance,
	RenderViewKind kind, const ResolvedRenderView& view) {

	// コンテキストの構築
	SceneExecutionContext context{};
	context.kind = kind;
	context.sceneInstance = sceneInstance;
	context.view = &view;
	// SceneViewの描画カメラはSceneViewのまま、カリングだけGameView基準にする
	context.cullingView = (kind == RenderViewKind::Scene && gameView_.valid) ? &gameView_ : &view;
	context.defaultSurface = viewportRenderService_->GetSurface(kind);
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;
	// 種類に応じたターゲットレジストリを選択
	RenderTargetRegistry* registry = kind == RenderViewKind::Game ?
		&gameViewTargetRegistry_ : &sceneViewTargetRegistry_;
	context.targetRegistry = registry;

	// フレーム開始処理
	registry->BeginFrame();

	// デフォルトのサーフェイスがある場合はレジストリに登録
	if (context.defaultSurface) {

		std::string colorName = ViewportRenderService::GetPrimaryColorName(kind);
		std::optional<std::string> depthName = std::string(ViewportRenderService::GetPrimaryDepthName(kind));
		registry->Register("View", context.defaultSurface, { colorName }, depthName);
		registry->Register(ViewportRenderService::GetViewAlias(kind), context.defaultSurface, { colorName }, depthName);
	}
	const SceneHeader* header = sceneInstance ? &sceneInstance->header : request.header;
	if (header) {
		for (const auto& renderTarget : header->renderTargets) {

			registry->ResizeTransient(graphicsCore, renderTarget, view.width, view.height);
		}
	}
	if (gameView_.valid) {

		// GameViewサイズのHZBを用意して、GameView/SceneViewの両方へ登録する
		gameViewHZB_.EnsureSize(graphicsCore, gameView_.width, gameView_.height);
		context.hzbCullingSource = &gameViewHZB_;
		if (kind == RenderViewKind::Game) {

			// HZBの生成はGameViewのDepthPrepass後だけで行う
			context.hzbBuildTarget = &gameViewHZB_;
		}
		gameViewHZB_.RegisterTo(context.bufferRegistry,
			graphicsCore.GetDXObject().GetFeatureController().ShouldUseOcclusionCulling());
	}
	// ビューごとのライトGPUバッファを登録
	switch (kind) {
	case RenderViewKind::Game:

		gameViewLightBuffers_.RegisterTo(context.bufferRegistry);
		gameViewLightCullingBuffers_.RegisterTo(context.bufferRegistry);
		gameViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	case RenderViewKind::Scene:

		sceneViewLightBuffers_.RegisterTo(context.bufferRegistry);
		sceneViewLightCullingBuffers_.RegisterTo(context.bufferRegistry);
		sceneViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	}
	return context;
}
