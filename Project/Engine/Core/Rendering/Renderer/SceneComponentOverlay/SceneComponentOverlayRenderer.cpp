#include "SceneComponentOverlayRenderer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayState.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>

// c++
#include <algorithm>
#include <filesystem>
#include <optional>

//============================================================================
//	local
//============================================================================
namespace {

	// RTV形式ごとにPipelineを共有するためのキー
	uint64_t MakePipelineKey(DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat) {

		return (static_cast<uint64_t>(rtvFormat) << 32) | static_cast<uint32_t>(dsvFormat);
	}

	// ライトアイコンPNGを通常アルファ合成で読むための固定サンプラ
	D3D12_STATIC_SAMPLER_DESC MakeLinearClampSampler() {

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		sampler.MipLODBias = 0.0f;
		sampler.MaxAnisotropy = 1;
		sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
		sampler.MinLOD = 0.0f;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		return sampler;
	}

	// Overlayの描画先RTV形式を取得する
	DXGI_FORMAT ResolveSurfaceRTVFormat(Engine::MultiRenderTarget& surface) {

		Engine::RenderTexture2D* color = surface.GetColorTexture(0);
		return color ? color->GetFormat() : DXGI_FORMAT_R32G32B32A32_FLOAT;
	}

	// Overlayアイコン用で色RTだけをBindして深度を完全に使わない
	bool BindColorOnly(Engine::GraphicsCore& graphicsCore, Engine::MultiRenderTarget& surface) {

		Engine::RenderTexture2D* color = surface.GetColorTexture(0);
		if (!color) {
			return false;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		color->Transition(*dxCommand, D3D12_RESOURCE_STATE_RENDER_TARGET);
		dxCommand->BindRenderTargets(std::optional<Engine::RenderTarget>(color->GetRenderTarget()), std::nullopt);
		dxCommand->SetViewportAndScissor(surface.GetWidth(), surface.GetHeight());
		return true;
	}
}

//============================================================================
//	SceneComponentOverlayRenderer classMethods
//============================================================================
Engine::SceneComponentOverlayRenderer::SceneComponentOverlayRenderer() {

	// Overlay専用シェーダのリソース名をRootBindingへ対応付ける
	spriteViewSlot_ = spriteBindingCache_.AddSlot("SceneOverlaySpriteView", ShaderBindingKind::CBV);
	spriteInstancesSlot_ = spriteBindingCache_.AddSlot("gSceneOverlaySpriteInstances", ShaderBindingKind::SRV);
	spriteTextureSlot_ = spriteBindingCache_.AddSlot("gSceneOverlayTexture", ShaderBindingKind::SRV);
}

Engine::SceneComponentOverlayRenderer::~SceneComponentOverlayRenderer() {

	Finalize();
}

void Engine::SceneComponentOverlayRenderer::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	// 通常RenderBatchとは別に、Overlay専用の小さなGPUバッファを持つ
	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	spriteView_.Init(device);

	initialized_ = true;
}

void Engine::SceneComponentOverlayRenderer::Finalize() {

	// Renderer寿命に合わせてOverlay専用GPUリソースを解放する
	for (auto& spriteInstances : spriteRunInstances_) {
		if (spriteInstances) {
			spriteInstances->Release();
		}
	}
	spriteRunInstances_.clear();
	pipelineCache_.clear();
	textureKeyCache_.clear();
	initialized_ = false;
}

Engine::SceneComponentOverlayRenderer::PipelinePair* Engine::SceneComponentOverlayRenderer::GetOrCreatePipelines(
	GraphicsCore& graphicsCore, DXGI_FORMAT rtvFormat) {

	const uint64_t key = MakePipelineKey(rtvFormat, DXGI_FORMAT_UNKNOWN);
	auto found = pipelineCache_.find(key);
	if (found != pipelineCache_.end()) {
		return &found->second;
	}

	// Spriteは深度なしでPNGアルファをそのまま重ねる
	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	DxShaderCompiler* compiler = graphicsCore.GetDXObject().GetDxShaderCompiler();

	PipelinePair pair{};
	pair.sprite = std::make_unique<PipelineState>();
	{
		GraphicsPipelineDesc desc{};
		desc.type = PipelineType::Vertex;
		desc.preRaster = { "edf35b0e885ae326", "main", "vs_6_0" };
		desc.pixel = { "feaf5c3be1a811cf", "main", "ps_6_0" };
		desc.staticSamplers.push_back(MakeLinearClampSampler());
		desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
		desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.depthStencil.DepthEnable = FALSE;
		desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.depthStencil.StencilEnable = FALSE;
		desc.sampleDesc = { 1, 0 };
		desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.numRenderTargets = 1;
		desc.rtvFormats[0] = rtvFormat;
		desc.dsvFormat = DXGI_FORMAT_UNKNOWN;
		if (!pair.sprite->CreateGraphics(device, compiler, desc)) {
			return nullptr;
		}
	}

	auto [it, inserted] = pipelineCache_.emplace(key, std::move(pair));
	return &it->second;
}

const Engine::GPUTextureResource* Engine::SceneComponentOverlayRenderer::ResolveReadyTexture(
	GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, AssetID textureAssetID) {

	if (!textureAssetID) {
		return nullptr;
	}

	auto keyIt = textureKeyCache_.find(textureAssetID);
	if (keyIt == textureKeyCache_.end()) {
		// GUIDから一度だけ実パスを解決し、以降はTextureUploadServiceのキーを使い回す
		const std::filesystem::path fullPath = assetDatabase.ResolveFullPath(textureAssetID);
		if (fullPath.empty()) {
			return nullptr;
		}
		const std::string key = fullPath.generic_string() + ":srgb";
		keyIt = textureKeyCache_.emplace(textureAssetID, key).first;
	}

	TextureUploadService& uploadService = graphicsCore.GetTextureUploadService();
	if (uploadService.GetState(keyIt->second) == TextureRequestState::None) {
		// 非同期ロード中は安全にスキップし、ロード完了後のフレームから描く
		TextureFileRequestDesc desc{};
		desc.key = keyIt->second;
		desc.assetPath = keyIt->second.substr(0, keyIt->second.size() - 5);
		desc.forceSRGB = true;
		uploadService.RequestTextureFile(desc);
		return nullptr;
	}
	if (uploadService.GetState(keyIt->second) != TextureRequestState::Ready) {
		return nullptr;
	}
	const GPUTextureResource* texture = uploadService.GetTexture(keyIt->second);
	return (texture && texture->valid) ? texture : nullptr;
}

Engine::StructuredInstanceBuffer<Engine::SceneComponentOverlayRenderer::SpriteInstanceData>&
Engine::SceneComponentOverlayRenderer::GetOrCreateSpriteRunBuffer(GraphicsCore& graphicsCore, size_t runIndex) {

	while (spriteRunInstances_.size() <= runIndex) {
		auto buffer = std::make_unique<StructuredInstanceBuffer<SpriteInstanceData>>("SceneOverlaySpriteInstances");
		buffer->Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
		buffer->EnsureCapacity(1);
		spriteRunInstances_.emplace_back(std::move(buffer));
	}
	return *spriteRunInstances_[runIndex];
}

void Engine::SceneComponentOverlayRenderer::Render(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
	const ResolvedRenderView& view, MultiRenderTarget& surface, DepthTexture2D* sceneDepth,
	ECSWorld* world, SceneComponentOverlayItemList& items) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	(void)sceneDepth;
	// 前フレームの描画済みアイテムがPickerへ残らないよう、最初に必ず消す
	SceneComponentOverlayState::GetInstance().Clear(world);
	if (items.empty() || !view.valid || surface.GetWidth() == 0 || surface.GetHeight() == 0) {
		return;
	}

	Init(graphicsCore);

	const DXGI_FORMAT rtvFormat = ResolveSurfaceRTVFormat(surface);
	PipelinePair* pipelines = GetOrCreatePipelines(graphicsCore, rtvFormat);
	if (!pipelines || !pipelines->sprite) {
		return;
	}

	SceneComponentOverlayItemList renderedItems{};
	renderedItems.reserve(items.size());

	// カメラもライトもSceneView前面へ、同じSprite Overlayとして描く
	DrawSpriteIcons(graphicsCore, assetDatabase, view, surface, *pipelines->sprite, items, renderedItems);

	// 実際に描けたアイテムだけをCPU Pickerへ渡す
	SceneComponentOverlayState::GetInstance().SetRenderedItems(world, renderedItems);
#else
	(void)graphicsCore;
	(void)assetDatabase;
	(void)view;
	(void)surface;
	(void)sceneDepth;
	(void)world;
	(void)items;
#endif
}

void Engine::SceneComponentOverlayRenderer::DrawSpriteIcons(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
	const ResolvedRenderView& view, MultiRenderTarget& surface, PipelineState& pipeline,
	SceneComponentOverlayItemList& items, SceneComponentOverlayItemList& renderedItems) {

	std::vector<const SceneComponentOverlayItem*> spriteItems{};
	spriteItems.reserve(items.size());
	for (const SceneComponentOverlayItem& item : items) {
		if (item.kind == SceneComponentOverlayKind::LightIcon ||
			item.kind == SceneComponentOverlayKind::CameraIcon) {
			spriteItems.push_back(&item);
		}
	}
	if (spriteItems.empty()) {
		return;
	}

	// 半透明Spriteは奥から手前へ描き近いものほど後に描かれ必ず前面に来る
	std::sort(spriteItems.begin(), spriteItems.end(),
		[](const SceneComponentOverlayItem* lhs, const SceneComponentOverlayItem* rhs) {
			if (lhs->viewDepth != rhs->viewDepth) {
				return lhs->viewDepth > rhs->viewDepth;
			}
			return lhs->stableOrder > rhs->stableOrder;
		});

	if (!BindColorOnly(graphicsCore, surface)) {
		return;
	}

	// 画面ピクセル矩形を6頂点のインスタンスQuadとして描く
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();
	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetGraphicsRootSignature(pipeline.GetRootSignature());
	commandList->SetPipelineState(pipeline.GetGraphicsPipeline(BlendMode::Normal));
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	SpriteViewConstants constants{};
	constants.viewSize = Vector2(static_cast<float>(view.width), static_cast<float>(view.height));
	spriteView_.Upload(constants);

	spriteBindingCache_.Sync(pipeline);
	if (!spriteBindingCache_.Has(spriteViewSlot_) ||
		!spriteBindingCache_.Has(spriteInstancesSlot_) ||
		!spriteBindingCache_.Has(spriteTextureSlot_)) {
		return;
	}
	RootBindingCommand::SetGraphicsCBV(commandList, spriteBindingCache_.Get(spriteViewSlot_),
		spriteView_.GetGPUAddress());

	size_t runBufferIndex = 0;
	size_t itemIndex = 0;
	while (itemIndex < spriteItems.size()) {

		const AssetID textureID = spriteItems[itemIndex]->asset;

		const GPUTextureResource* texture = ResolveReadyTexture(graphicsCore, assetDatabase, textureID);
		if (!texture) {
			++itemIndex;
			continue;
		}

		// 深度順を崩さない範囲で、連続する同一テクスチャだけをまとめる
		spriteScratch_.clear();
		const size_t runBegin = itemIndex;
		while (itemIndex < spriteItems.size() && spriteItems[itemIndex]->asset == textureID) {
			const SceneComponentOverlayItem& item = *spriteItems[itemIndex];

			SpriteInstanceData instance{};
			instance.center = item.screenCenter;
			instance.halfSize = (item.rectMax - item.rectMin) * 0.5f;
			instance.color = item.color;
			instance.rotationRadians = item.spriteRotationRadians;
			spriteScratch_.push_back(instance);
			++itemIndex;
		}

		// 描画できたOverlayだけPicker候補へ残す
		if (spriteScratch_.empty()) {
			continue;
		}
		// runごとに別バッファへ積みGPU実行前に後続Uploadで前のDraw元を上書きしないため
		auto& spriteInstances = GetOrCreateSpriteRunBuffer(graphicsCore, runBufferIndex++);
		spriteInstances.Upload(spriteScratch_);
		RootBindingCommand::SetGraphicsSRV(commandList, spriteBindingCache_.Get(spriteInstancesSlot_),
			spriteInstances.GetGPUAddress(), spriteInstances.GetGPUHandle());
		RootBindingCommand::SetGraphicsSRV(commandList, spriteBindingCache_.Get(spriteTextureSlot_),
			0, texture->gpuHandle);
		commandList->DrawInstanced(6, static_cast<UINT>(spriteScratch_.size()), 0, 0);

		for (size_t renderedIndex = runBegin; renderedIndex < itemIndex; ++renderedIndex) {
			renderedItems.push_back(*spriteItems[renderedIndex]);
		}
	}
}
