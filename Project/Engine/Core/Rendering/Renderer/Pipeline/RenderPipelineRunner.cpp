#include "RenderPipelineRunner.h"
#include "RenderPipelineUtility.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Renderer/Views/GameViewCameraSnapshot.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/FillMesh/FillMeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/FillMesh/FillMeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/BuiltinLightExtractors.h>
#include <Engine/Core/Rendering/Renderer/Lighting/ViewLightCollector.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

namespace {

	// 描画パスが使うRTVとDSVの組み合わせ
	struct RuntimeTargetFormats {

		std::vector<DXGI_FORMAT> rtvFormats{};
		DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
	};

	// GBuffer用のMRT形式を構築する
	RuntimeTargetFormats MakeSceneMainFormats() {

		RuntimeTargetFormats formats{};
		formats.rtvFormats = {
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_R8G8B8A8_UNORM,
			DXGI_FORMAT_R11G11B10_FLOAT,
			DXGI_FORMAT_R32_UINT,
		};
		formats.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		return formats;
	}

	// マテリアルパスの実行先形式を解決する
	RuntimeTargetFormats ResolvePassFormats(const Engine::MaterialAsset& material,
		Engine::MaterialPassKind passKind) {

		RuntimeTargetFormats formats{};
		if (material.domain == Engine::MaterialDomain::UI ||
			material.domain == Engine::MaterialDomain::Fullscreen) {

			formats.rtvFormats = { DXGI_FORMAT_R32G32B32A32_FLOAT };
			return formats;
		}
		switch (passKind) {
		case Engine::MaterialPassKind::ZPrepass:
			formats.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			break;
		case Engine::MaterialPassKind::EditorPicking:
			formats.rtvFormats = { DXGI_FORMAT_R32G32B32A32_UINT };
			formats.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			break;
		case Engine::MaterialPassKind::Draw:
			formats = MakeSceneMainFormats();
			break;
		case Engine::MaterialPassKind::ScreenSpaceOutlineMask:
		case Engine::MaterialPassKind::ScreenSpaceOutlineCoverageMask:
			formats.rtvFormats = { DXGI_FORMAT_R16_UINT };
			formats.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			break;
		case Engine::MaterialPassKind::Transparent:
		case Engine::MaterialPassKind::Outline:
		case Engine::MaterialPassKind::OutlineStencilWrite:
		case Engine::MaterialPassKind::OutlineStencilTest:
			formats.rtvFormats = { DXGI_FORMAT_R32G32B32A32_FLOAT };
			formats.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
			break;
		default:
			formats.rtvFormats = { DXGI_FORMAT_R32G32B32A32_FLOAT };
			break;
		}
		return formats;
	}
}

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================

void RenderPipelineRunner::Init() {

	viewportRenderService_ = std::make_unique<ViewportRenderService>();

	// 描画アイテム抽出器の登録
	extractorRegistry_.Clear();
	extractorRegistry_.Register(std::make_unique<SpriteRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<TextRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<MeshRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<LineRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<FillMeshRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<PrimitiveRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<ParticleRenderItemExtractor>());
	// 描画バックエンドの登録
	backendRegistry_.Clear();
	backendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	backendRegistry_.Register(std::make_unique<TextRenderBackend>());
	backendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	backendRegistry_.Register(std::make_unique<LineRenderBackend>());
	backendRegistry_.Register(std::make_unique<FillMeshRenderBackend>());
	backendRegistry_.Register(std::make_unique<PrimitiveRenderBackend>());
	backendRegistry_.Register(std::make_unique<ParticleRenderBackend>());
	// ツールプレビューはメインビューとは別のGPUバッファを持たせる
	previewBackendRegistry_.Clear();
	previewBackendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<TextRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<LineRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<FillMeshRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<PrimitiveRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<ParticleRenderBackend>());
	// 型付きMeshバックエンドをキャッシュして毎フレームのdynamic_castを避ける
	meshBackend_ = dynamic_cast<MeshRenderBackend*>(backendRegistry_.Find(RenderBackendID::Mesh));
	previewMeshBackend_ = dynamic_cast<MeshRenderBackend*>(previewBackendRegistry_.Find(RenderBackendID::Mesh));
	primitiveBackend_ = dynamic_cast<PrimitiveRenderBackend*>(backendRegistry_.Find(RenderBackendID::Primitive));
	particleBackend_ = dynamic_cast<ParticleRenderBackend*>(backendRegistry_.Find(RenderBackendID::Particle));
	// ライト抽出器の登録
	lightExtractorRegistry_.Clear();
	lightExtractorRegistry_.Register(std::make_unique<DirectionalLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<PointLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<SpotLightExtractor>());

	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	postProcessExecutor_.Release();
	postProcessAssetGenerator_.Clear();
	frameLightBatch_.Clear();
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	previewLightSet_.Clear();

	raytracingPipelineStateCache_.Clear();
	gameViewState_.raytracingBuffers.Release();
	sceneViewState_.raytracingBuffers.Release();

	// 固定RenderPathの初期化
	{
		RenderPipelineDeps deps{};
		deps.renderBatch = &renderBatch_;
		deps.backendRegistry = &backendRegistry_;
		deps.assetLibrary = &renderAssetLibrary_;
		deps.pipelineCache = &pipelineStateCache_;
		deps.materialResolver = &materialResolver_;
		deps.raytracingPipelineCache = &raytracingPipelineStateCache_;
		deps.postProcessExecutor = &postProcessExecutor_;
		deps.postProcessTargetPool = &postProcessTargetPool_;
		deps.postProcessDebugInjector = &postProcessDebugInjector_;
		deps.postProcessAssetGenerator = &postProcessAssetGenerator_;
		deps.dispatcher = &batchDispatcher_;
		renderPath_.Initialize(deps);
	}

	// ビューライトバッファの初期化
	gameViewState_.lightBuffers.Release();
	sceneViewState_.lightBuffers.Release();
	previewLightBufferPool_.Clear();
	previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
}

void RenderPipelineRunner::PreloadRuntimeAssets(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase) {

	Logger::Output(LogType::Engine, "[RuntimePreload] Start");
	renderAssetLibrary_.Init(&assetDatabase);
	postProcessAssetGenerator_.EnsureBuiltinAssets(&assetDatabase);

	std::vector<const AssetMeta*> assets{};
	assets.reserve(assetDatabase.GetAssets().size());
	for (const auto& [assetID, meta] : assetDatabase.GetAssets()) {
		assets.emplace_back(&meta);
	}
	std::sort(assets.begin(), assets.end(), [](const AssetMeta* lhs, const AssetMeta* rhs) {
		return lhs->assetPath < rhs->assetPath;
		});

	std::vector<AssetID> meshAssets{};
	std::vector<AssetID> materialAssets{};
	std::vector<AssetID> pipelineAssets{};
	std::vector<AssetID> postProcessAssets{};
	TextureUploadService& textureUploadService = graphicsCore.GetTextureUploadService();
	for (const AssetMeta* meta : assets) {

		switch (meta->type) {
		case AssetType::Texture:
		{
			const std::filesystem::path fullPath = assetDatabase.ResolveFullPath(meta->guid);
			if (fullPath.empty()) {
				break;
			}
			const std::string texturePath = fullPath.generic_string();
			for (bool sRGB : { false, true }) {

				TextureFileRequestDesc desc{};
				desc.key = sRGB ? texturePath + ":srgb" : texturePath;
				desc.assetPath = texturePath;
				desc.forceSRGB = sRGB;
				textureUploadService.RequestTextureFile(desc);
			}
			break;
		}
		case AssetType::Material:
			renderAssetLibrary_.LoadMaterial(meta->guid);
			materialAssets.emplace_back(meta->guid);
			break;
		case AssetType::Shader:
		{
			const std::string path = Algorithm::ToLower(meta->assetPath);
			if (Algorithm::EndsWith(path, ".shader.json") ||
				Algorithm::EndsWith(path, ".shader")) {
				renderAssetLibrary_.LoadShader(meta->guid);
			}
			break;
		}
		case AssetType::RenderPipeline:
			renderAssetLibrary_.LoadPipeline(meta->guid);
			pipelineAssets.emplace_back(meta->guid);
			break;
		case AssetType::Font:
		{
			const std::string path = Algorithm::ToLower(meta->assetPath);
			if (Algorithm::EndsWith(path, ".font.json") ||
				Algorithm::EndsWith(path, ".msdf.json") ||
				Algorithm::EndsWith(path, ".font")) {
				renderAssetLibrary_.LoadFont(meta->guid);
			}
			break;
		}
		case AssetType::ParticleEffect:
			renderAssetLibrary_.LoadParticleEffect(meta->guid);
			break;
		case AssetType::Mesh:
			meshAssets.emplace_back(meta->guid);
			break;
		case AssetType::PostProcessStack:
			postProcessAssets.emplace_back(meta->guid);
			break;
		default:
			break;
		}
	}

	// 全テクスチャのCPUデコードとGPU転送を完了する
	textureUploadService.WaitAll();

	// 通常メッシュとModel Particleは別キャッシュなので両方作成する
	backendRegistry_.BeginFrame(graphicsCore);
	if (meshBackend_) {
		meshBackend_->PreloadMeshes(graphicsCore, assetDatabase, meshAssets);
	}
	if (particleBackend_) {
		particleBackend_->PreloadMeshes(graphicsCore, assetDatabase, meshAssets);
	}
	graphicsCore.GetBufferUploadService().FlushAndWait();

	// 初回描画で解像度依存のRTを作らないように先に確保する
	const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
	const uint32_t width = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.x));
	const uint32_t height = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.y));
	viewportRenderService_->SyncSurface(graphicsCore, RenderViewKind::Game, width, height);
	gameViewState_.resources.Resize(graphicsCore, width, height);

	const GraphicsRuntimeFeatures& runtimeFeatures =
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const DXGI_FORMAT backBufferFormat = graphicsCore.GetBackBufferRenderTarget().format;
	size_t pipelineCount = 0;
	auto preloadPipeline = [&](const MaterialAsset& material, const MaterialPassBinding& pass,
		const RuntimeTargetFormats& formats, bool forceDepthTestWrite = false) {

		const PipelineState* pipeline = nullptr;
		if (pass.preferredVariant == PipelineVariantKind::Raytracing) {
			pipeline = nullptr;
			raytracingPipelineStateCache_.GetOrCreate(
				graphicsCore.GetDXObject(), renderAssetLibrary_, pass.pipeline);
		} else if (pass.preferredVariant == PipelineVariantKind::Compute) {

			PipelineStaticSamplerOverrideSet samplerOverrides{};
			samplerOverrides.fillMissingSamplers = true;
			pipeline = pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(), renderAssetLibrary_,
				pass.pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN,
				runtimeFeatures, nullptr, false, &samplerOverrides);
		} else if (pass.shaderOverride) {
			pipeline = pipelineStateCache_.GetORCreateComposed(graphicsCore.GetDXObject(), renderAssetLibrary_,
				pass.pipeline, pass.pipeline, pass.shaderOverride, pass.preferredVariant,
				formats.rtvFormats, formats.dsvFormat, runtimeFeatures);
		} else {
			pipeline = pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(), renderAssetLibrary_,
				pass.pipeline, pass.preferredVariant, formats.rtvFormats, formats.dsvFormat,
				runtimeFeatures, nullptr, forceDepthTestWrite);
		}
		if (pipeline || pass.preferredVariant == PipelineVariantKind::Raytracing) {
			++pipelineCount;
		}

		// Particleの専用MS形状とTrailはMaterialのPSと先に合成する
		if (material.usage == MaterialUsage::Particle &&
			pass.preferredVariant != PipelineVariantKind::Compute &&
			pass.preferredVariant != PipelineVariantKind::Raytracing) {

			for (AssetID geometryPipeline : {
				BuiltinAssets::Pipelines::ParticleRingMS,
				BuiltinAssets::Pipelines::ParticleCylinderMS }) {

				if (pipelineStateCache_.GetORCreateComposed(graphicsCore.GetDXObject(), renderAssetLibrary_,
					pass.pipeline, geometryPipeline, pass.shaderOverride, PipelineVariantKind::GraphicsMesh,
					formats.rtvFormats, formats.dsvFormat, runtimeFeatures)) {
					++pipelineCount;
				}
			}
			const PipelineVariantKind trailKind = runtimeFeatures.useMeshShader ?
				PipelineVariantKind::GraphicsMesh : PipelineVariantKind::GraphicsVertex;
			if (pipelineStateCache_.GetORCreateComposed(graphicsCore.GetDXObject(), renderAssetLibrary_,
				pass.pipeline, BuiltinAssets::Pipelines::ParticleTrail, pass.shaderOverride, trailKind,
				formats.rtvFormats, formats.dsvFormat, runtimeFeatures)) {
				++pipelineCount;
			}
		}
	};

	for (AssetID materialID : materialAssets) {

		const MaterialAsset* material = renderAssetLibrary_.LoadMaterial(materialID);
		if (!material) {
			continue;
		}
		for (const MaterialPassBinding& pass : material->passes) {

			const RuntimeTargetFormats formats = ResolvePassFormats(*material, pass.passKind);
			preloadPipeline(*material, pass, formats);
			if (material->usage == MaterialUsage::Text &&
				pass.preferredVariant != PipelineVariantKind::Compute &&
				pass.preferredVariant != PipelineVariantKind::Raytracing) {
				preloadPipeline(*material, pass, formats, true);
			}
			if (pass.passKind == MaterialPassKind::Blit || pass.passKind == MaterialPassKind::Fullscreen) {

				RuntimeTargetFormats backBufferFormats{};
				backBufferFormats.rtvFormats = { backBufferFormat };
				preloadPipeline(*material, pass, backBufferFormats);
			}
		}
	}

	// Materialから参照されないComputeやDXRパイプラインも作成する
	for (AssetID pipelineID : pipelineAssets) {

		const RenderPipelineAsset* pipelineAsset = renderAssetLibrary_.LoadPipeline(pipelineID);
		if (!pipelineAsset) {
			continue;
		}
		for (const PipelineVariantDesc& variant : pipelineAsset->variants) {

			if (variant.kind == PipelineVariantKind::Raytracing) {
				if (raytracingPipelineStateCache_.GetOrCreate(
					graphicsCore.GetDXObject(), renderAssetLibrary_, pipelineID)) {
					++pipelineCount;
				}
				continue;
			}
			if (variant.kind == PipelineVariantKind::Compute) {

				if (pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(), renderAssetLibrary_,
					pipelineID, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN,
					runtimeFeatures)) {
					++pipelineCount;
				}
				continue;
			}

			RuntimeTargetFormats formats{};
			if (variant.numRenderTargets == 0) {
				formats.dsvFormat = variant.dsvFormat != DXGI_FORMAT_UNKNOWN ?
					variant.dsvFormat : DXGI_FORMAT_D24_UNORM_S8_UINT;
			} else if (3 <= variant.numRenderTargets) {
				formats = MakeSceneMainFormats();
				formats.rtvFormats.resize((std::min)(formats.rtvFormats.size(),
					static_cast<size_t>(variant.numRenderTargets)));
			} else {
				formats.rtvFormats.assign(variant.numRenderTargets, DXGI_FORMAT_R32G32B32A32_FLOAT);
				formats.dsvFormat = variant.depthStencil.DepthEnable ?
					DXGI_FORMAT_D24_UNORM_S8_UINT : DXGI_FORMAT_UNKNOWN;
			}
			if (pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(), renderAssetLibrary_,
				pipelineID, variant.kind, formats.rtvFormats, formats.dsvFormat, runtimeFeatures)) {
				++pipelineCount;
			}
		}
	}

	// PostProcessはSampler上書きもPSOキーに含むためStackごとに作成する
	for (AssetID stackID : postProcessAssets) {

		PostProcessStackSettings stack{};
		if (!PostProcessStackSerializer::Load(assetDatabase.ResolveFullPath(stackID), stack)) {
			continue;
		}
		for (const PostProcessStackPassSettings& stackPass : stack.passes) {

			const MaterialAsset* material = renderAssetLibrary_.LoadMaterial(stackPass.materialGuid);
			const MaterialPassBinding* pass = material ? FindPass(*material, stackPass.passKind) : nullptr;
			if (!pass || pass->preferredVariant != PipelineVariantKind::Compute) {
				continue;
			}
			PipelineStaticSamplerOverrideSet samplerOverrides{};
			samplerOverrides.fillMissingSamplers = true;
			samplerOverrides.byName = stackPass.samplerOverrides;
			if (pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(), renderAssetLibrary_,
				pass->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN,
				runtimeFeatures, nullptr, false, &samplerOverrides)) {
				++pipelineCount;
			}
		}
	}

	Logger::Output(LogType::Engine,
		"[RuntimePreload] Assets={} Textures={} Meshes={} Materials={} Pipelines={}",
		assets.size(),
		static_cast<size_t>(std::count_if(assets.begin(), assets.end(), [](const AssetMeta* meta) {
			return meta->type == AssetType::Texture;
			})),
		meshAssets.size(), materialAssets.size(), pipelineCount);
}

void RenderPipelineRunner::ReloadMesh(AssetID meshAssetID) {

	// 本番用とプレビュー用の両メッシュバックエンドへ再ロードを伝える
	if (meshBackend_) {
		meshBackend_->RequestMeshReload(meshAssetID);
	}
	if (previewMeshBackend_) {
		previewMeshBackend_->RequestMeshReload(meshAssetID);
	}
}

void RenderPipelineRunner::ReloadMaterial(AssetID materialAssetID) {

	// マテリアルキャッシュを破棄して次フレームのLoadMaterialでファイルから読み直させる
	// インスペクタでの編集を実行中に即反映するため
	renderAssetLibrary_.InvalidateMaterial(materialAssetID);
}

void RenderPipelineRunner::ReloadShader(AssetID shaderAssetID) {

	// 新PSO生成に失敗した場合は退避した旧PSOを継続使用する
	renderAssetLibrary_.InvalidateShader(shaderAssetID);
	pipelineStateCache_.InvalidateByShaderAsset(shaderAssetID);
}

void RenderPipelineRunner::ReloadPipeline(AssetID pipelineAssetID) {

	renderAssetLibrary_.InvalidatePipeline(pipelineAssetID);
	pipelineStateCache_.InvalidateByPipelineAsset(pipelineAssetID);
}

void RenderPipelineRunner::ReloadAsset(AssetDatabase& assetDatabase, AssetID assetID) {

	const AssetMeta* meta = assetDatabase.Find(assetID);
	if (!meta) {
		return;
	}

	// JSONの参照先が変わった場合に備えて逆引き依存関係も更新する
	assetDatabase.RefreshDependencies(assetID);
	if (meta->type == AssetType::Material) {
		ReloadMaterial(assetID);
		return;
	}
	if (meta->type == AssetType::RenderPipeline) {
		ReloadPipeline(assetID);
		return;
	}
	if (meta->type != AssetType::Shader) {
		return;
	}

	const std::filesystem::path path(meta->assetPath);
	if (Algorithm::ToLower(path.extension().string()) == ".json") {
		ReloadShader(assetID);
		return;
	}

	// HLSL変更時は参照するshader.jsonを再ロードして依存PSOを再生成する
	const std::vector<AssetID> referencers = assetDatabase.FindReferencers(assetID);
	for (AssetID referencer : referencers) {
		const AssetMeta* referencerMeta = assetDatabase.Find(referencer);
		if (referencerMeta && referencerMeta->type == AssetType::Shader) {
			ReloadShader(referencer);
		}
	}
}

Engine::RenderTexture2D* RenderPipelineRunner::GetViewGBufferTexture(RenderViewKind kind, GBufferAttachment attachment) {

	// GameViewはgameViewState_.resources、それ以外はsceneViewState_.resourcesのGBufferを参照する
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewState_.resources : sceneViewState_.resources;
	return resources.GetGBuffer(attachment);
}

const Engine::ShaderReflectionInfo* RenderPipelineRunner::FindMaterialDrawReflection(const MaterialAsset& material) const {

	const MaterialPassBinding* drawPass = FindPass(material, MaterialPassKind::Draw);
	if (drawPass && drawPass->pipeline) {

		const ShaderReflectionInfo* reflection =
			pipelineStateCache_.FindGraphicsReflection(drawPass->pipeline, drawPass->shaderOverride);
		if (reflection) {
			return reflection;
		}
	}

	const MaterialPassBinding* transparentPass = FindPass(material, MaterialPassKind::Transparent);
	if (transparentPass && transparentPass->pipeline) {
		return pipelineStateCache_.FindGraphicsReflection(transparentPass->pipeline, transparentPass->shaderOverride);
	}
	return nullptr;
}

Engine::DepthTexture2D* RenderPipelineRunner::GetViewDepthTexture(RenderViewKind kind) {

	// 深度はGBufferの色ではなくSceneMainの深度アタッチメントを参照する
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewState_.resources : sceneViewState_.resources;
	MultiRenderTarget* sceneMain = resources.GetSceneMain();
	return sceneMain ? sceneMain->GetDepthTexture() : nullptr;
}

void RenderPipelineRunner::Finalize() {

	// GPU計測用のクエリヒープ/リードバックバッファはここで解放する
	// シングルトンのため放置するとDeviceより後まで生き残り、LeakCheckerに残る
	GPUFrameProfiler::GetInstance().Finalize();

	renderPath_.Finalize();
	backendRegistry_.Clear();
	previewBackendRegistry_.Clear();
	meshBackend_ = nullptr;
	primitiveBackend_ = nullptr;
	particleBackend_ = nullptr;
	previewMeshBackend_ = nullptr;
	extractorRegistry_.Clear();
	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	postProcessExecutor_.Release();
	postProcessAssetGenerator_.Clear();
	lightExtractorRegistry_.Clear();
	frameLightBatch_.Clear();
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	previewLightSet_.Clear();
	gameViewState_.lightBuffers.Release();
	sceneViewState_.lightBuffers.Release();
	previewLightBufferPool_.Clear();
	if (viewportRenderService_) {
		viewportRenderService_->Finalize();
		viewportRenderService_.reset();
	}
	raytracingPipelineStateCache_.Clear();
	gameViewState_.raytracingBuffers.Release();
	sceneViewState_.raytracingBuffers.Release();
	previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
	lastRenderRequest_ = {};
	lastActiveScene_ = nullptr;
	raytracingSceneBuilder_.Finalize();
	gameViewState_.resources.Destroy();
	sceneViewState_.resources.Destroy();
}

void RenderPipelineRunner::Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {


	// エディタPostSceneで複数のプレビューを描画するため、プレビューbackendのリソースプールは
	// ここでフレーム境界だけリセットし、RenderEntityPreviewごとにはリセットしない
	previewBackendFrameStarted_ = false;

	// ワールドがない場合は描画できないので処理しない
	if (!request.world) {
		lastRenderRequest_ = {};
		lastActiveScene_ = nullptr;
		return;
	}

	// データクリア
	tlasResource_ = nullptr;
	pickRecords_.clear();
	pickRecordOffsets_.clear();

	// アセットライブラリの初期化、フレーム開始処理
	renderAssetLibrary_.Init(request.assetDatabase);
	postProcessAssetGenerator_.EnsureBuiltinAssets(request.assetDatabase);
	postProcessExecutor_.BeginFrame(request.systemContext->unscaledDeltaTime);
	backendRegistry_.BeginFrame(graphicsCore);

	// ワールドが切り替わった場合は静的バッチキャッシュを即時破棄してSRV重複確保を防ぐ
	if (request.world != lastRenderedWorld_) {
		if (meshBackend_) {
			meshBackend_->ClearStaticBatchCache();
		}
		if (previewMeshBackend_) {
			previewMeshBackend_->ClearStaticBatchCache();
		}
		lastRenderedWorld_ = request.world;
	}

	// レイトレシーンフレーム開始処理
	raytracingSceneBuilder_.BeginFrame(graphicsCore);

	// 描画要求に基づいて必要なサーフェイスをGPUと同期し、ビュー情報を決定
	SyncRequestedSurfaces(graphicsCore, request);
	ResolveViews(request);

	// デスクリプタヒープの一括設定
	graphicsCore.GetDXObject().GetDxCommand()->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap()
		});

	// GPU計測のフレーム開始(前フレームの結果をFrameProfilerへ反映し、記録をリセット)
	GPUFrameProfiler::GetInstance().BeginFrame(graphicsCore.GetDXObject().GetDevice(),
		graphicsCore.GetDXObject().GetCommandQueue()->GetQueue());

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
	lastRenderRequest_ = request;
	lastActiveScene_ = activeScene;

	// シーン切り替え時にPostProcessStack設定をサービスへ通知する
	if (activeScene) {

		const AssetID ppAsset = activeScene->header.postProcessStack;
		if (ppAsset != lastNotifiedPostProcessStack_) {

			PostProcessStackService& service = PostProcessStackService::GetInstance();
			if (!service.IsDirty()) {
				service.SetActiveSettingsAsset(ppAsset, request.assetDatabase);
			}
			lastNotifiedPostProcessStack_ = ppAsset;
		}
	}

	// メッシュ描画クラスの取得(Initでキャッシュ済み)
	MeshRenderBackend* meshBackend = meshBackend_;

	// 毎フレーム使い回すスクラッチをクリアする(容量は保持して再確保を避ける)
	visibleMeshSet_.clear();
	visibleMeshSet_.reserve(renderBatch_.GetItems().size());

	// ビューごとに可視なメッシュアセットIDを収集
	if (meshBackend && activeScene) {
		if (gameViewState_.view.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, gameViewState_.view, visibleMeshSet_);
		}
		if (sceneViewState_.view.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, sceneViewState_.view, visibleMeshSet_);
		}
	}

	visibleMeshes_.clear();
	visibleMeshes_.reserve(visibleMeshSet_.size());
	for (const AssetID& id : visibleMeshSet_) {
		visibleMeshes_.emplace_back(id);
	}
	// GPUに可視なメッシュの情報を要求して、必要なリソースを準備
	if (meshBackend && !visibleMeshes_.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, visibleMeshes_);
	}

	// ビューごとのライト集合クリア
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	// ルートシーン用のビューライト構築
	if (activeScene) {
		if (gameViewState_.view.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, gameViewState_.view, gameViewState_.lightSet);
		}
		if (sceneViewState_.view.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, sceneViewState_.view, sceneViewState_.lightSet);
		}
	}

	// GPUライトバッファ初期化
	if (!gameViewState_.lightBuffers.IsInitialized()) {
		gameViewState_.lightBuffers.Init(graphicsCore);
	}
	if (!sceneViewState_.lightBuffers.IsInitialized()) {
		sceneViewState_.lightBuffers.Init(graphicsCore);
	}
	// ビューごとのライト集合をGPUへ転送
	gameViewState_.lightBuffers.Upload(gameViewState_.lightSet);
	sceneViewState_.lightBuffers.Upload(sceneViewState_.lightSet);

	// レイトレーシングビュー関連バッファの初期化と転送
	if (!gameViewState_.raytracingBuffers.IsInitialized()) {
		gameViewState_.raytracingBuffers.Init(graphicsCore);
	}
	if (!sceneViewState_.raytracingBuffers.IsInitialized()) {
		sceneViewState_.raytracingBuffers.Init(graphicsCore);
	}
	// 反射レイのミス時に参照するskyboxを解決して渡す
	const SceneSkyboxInfo skyboxInfo = SceneSkyboxResolver::Resolve(graphicsCore, request.assetDatabase, request.world);
	gameViewState_.raytracingBuffers.Upload(gameViewState_.view, skyboxInfo);
	sceneViewState_.raytracingBuffers.Upload(sceneViewState_.view, skyboxInfo);

	// スクリプトのScreenPointToRay用にGameViewカメラのスナップショットを更新する
	GameViewCameraSnapshot::Snapshot cameraSnapshot{};
	if (gameViewState_.view.valid) {
		if (const ResolvedCameraView* gameCamera = gameViewState_.view.FindCamera(RenderCameraDomain::Perspective);
			gameCamera && gameCamera->valid) {

			cameraSnapshot.viewProjection = gameCamera->matrices.viewProjectionMatrix;
			cameraSnapshot.inverseViewProjection =
				gameCamera->matrices.inverseProjectionMatrix * gameCamera->matrices.inverseViewMatrix;
			cameraSnapshot.cameraPos = gameCamera->cameraPos;
			cameraSnapshot.width = static_cast<float>(gameViewState_.view.width);
			cameraSnapshot.height = static_cast<float>(gameViewState_.view.height);
			cameraSnapshot.valid = true;
		}
	}
	GameViewCameraSnapshot::Set(cameraSnapshot);

	// 描画ビューごとに描画を実行
	auto renderView = [&](RenderViewKind kind, const ResolvedRenderView& view) {
		if (!view.valid) {
			return;
		}

		SceneExecutionContext context = BuildViewExecutionContext(graphicsCore, request, activeScene, kind, view);
		if (!context.sceneInstance) {
			return;
		}
		// バケットはメンバを使い回して内部vectorの容量を保持する(BuildBucketsForViewAndScene内でClearされる)
		RenderPassItemCollector::BuildBucketsForViewAndScene(
			renderBatch_, view, context.sceneInstance->instanceID, passBuckets_);

		ID3D12GraphicsCommandList6* commandList =
			graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
		const std::string viewName = std::string(EnumAdapter<RenderViewKind>::ToStringView(kind));

		// スキニングメッシュの頂点更新
		if (meshBackend) {
			GPUFrameProfiler::GetInstance().BeginPass(commandList, viewName + "/Skinning");
			PreDispatchVisibleMeshSkinning(graphicsCore, context,
				renderBatch_, backendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets_);
			GPUFrameProfiler::GetInstance().EndPass(commandList);

			// レイトレーシングシーンの構築
			// gRaytracingSceneInstances/gRaytracingSubMeshesはcontext.bufferRegistryへ登録する必要があるため、
			// コピーではなく実際のcontextへ直接構築する、コピーへ構築すると登録が破棄され反射パスが早期リターンする
			// TLAS構築の基準ビューだけ一時的にGameViewへ差し替え、構築後に元へ戻す
			const ResolvedRenderView* prevTlasView = context.view;
			if (gameViewState_.view.valid) {
				context.view = &gameViewState_.view;
			}
			PrimitiveGeometryManager* primitiveGeometryManager = primitiveBackend_ ? &primitiveBackend_->GetGeometryManager() : nullptr;
			GPUFrameProfiler::GetInstance().BeginPass(commandList, viewName + "/RaytracingSceneBuild");
			raytracingSceneBuilder_.BuildForScene(graphicsCore, *request.assetDatabase, meshBackend, primitiveGeometryManager, renderBatch_, context);
			GPUFrameProfiler::GetInstance().EndPass(commandList);
			context.view = prevTlasView;

			if (context.raytracing.tlasResource) {
				tlasResource_ = context.raytracing.tlasResource;
				pickRecords_ = raytracingSceneBuilder_.GetPickRecords();
				pickRecordOffsets_ = raytracingSceneBuilder_.GetPickRecordOffsets();
			}
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

		// 固定RenderPathを実行
		renderPath_.Execute(graphicsCore, passBuckets_, context);

		// 終了後に全ターゲットをシェーダーリード状態へ遷移
		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		for (MultiRenderTarget* surface : context.targetRegistry->GatherUniqueSurfaces()) {
			surface->TransitionForShaderRead(*dxCommand);
		}
		if (context.resources) {
			if (context.resources->GetSceneMain()) {
				context.resources->GetSceneMain()->TransitionForShaderRead(*dxCommand);
			}
			if (context.resources->GetSceneFinal()) {
				context.resources->GetSceneFinal()->TransitionForShaderRead(*dxCommand);
			}
		}
		};
	renderView(RenderViewKind::Game, gameViewState_.view);
	renderView(RenderViewKind::Scene, sceneViewState_.view);

	// 記録したパスのタイムスタンプを解決してリードバックバッファへ書き出す
	GPUFrameProfiler::GetInstance().Resolve(graphicsCore.GetDXObject().GetDxCommand()->GetCommandList());
}

bool RenderPipelineRunner::RenderMeshPicking(GraphicsCore& graphicsCore,
	RenderViewKind kind, const Vector2& inputPixel,
	MultiRenderTarget& target) {

	if (!lastRenderRequest_.world || !lastRenderRequest_.assetDatabase ||
		!lastActiveScene_ || !target.IsValid()) {
		return false;
	}

	const ResolvedRenderView& view = GetResolvedView(kind);
	const ResolvedCameraView* camera =
		view.FindCamera(RenderCameraDomain::Perspective);
	if (!view.valid || !camera) {
		return false;
	}

	std::vector<const RenderItem*> items{};
	items.reserve(renderBatch_.GetItems().size());
	for (const RenderItem& item : renderBatch_.GetItems()) {

		if (item.backendID != RenderBackendID::Mesh ||
			item.sceneInstanceID != lastActiveScene_->instanceID ||
			item.cameraDomain != RenderCameraDomain::Perspective ||
			(item.visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}
		items.emplace_back(&item);
	}
	SceneExecutionContext context{};
	context.kind = kind;
	context.sceneInstance = lastActiveScene_;
	context.view = &view;
	context.cullingView = &view;
	context.defaultSurface = &target;
	context.billboardView =
		(kind == RenderViewKind::Scene && gameViewState_.view.valid) ?
		&gameViewState_.view : &view;
	context.disableInlineRayTracing = true;
	context.forceVertexMeshVariant = true;
	context.world = lastRenderRequest_.world;
	context.systemContext = lastRenderRequest_.systemContext;
	context.assetDatabase = lastRenderRequest_.assetDatabase;

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap()
		});
	target.TransitionForRender(*dxCommand);
	target.Bind(*dxCommand);
	target.Clear(*dxCommand, {
		.clearColor = true,
		.clearColorValue = Color4::Black(),
		.clearDepth = true,
		.clearDepthValue = 1.0f,
		});

	// 元ビューを負のオフセットで1x1 RTへ写し、クリック画素だけをラスタライズする
	const float pixelX = std::floor(std::clamp(
		inputPixel.x, 0.0f, static_cast<float>(view.width - 1)));
	const float pixelY = std::floor(std::clamp(
		inputPixel.y, 0.0f, static_cast<float>(view.height - 1)));
	D3D12_VIEWPORT viewport{};
	viewport.TopLeftX = -pixelX;
	viewport.TopLeftY = -pixelY;
	viewport.Width = static_cast<float>(view.width);
	viewport.Height = static_cast<float>(view.height);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	D3D12_RECT scissor{ 0, 0, 1, 1 };

	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	batchDispatcher_.Dispatch(graphicsCore, context, renderBatch_,
		backendRegistry_, renderAssetLibrary_, pipelineStateCache_,
		materialResolver_, items, &target, nullptr,
		MaterialPassKind::EditorPicking, false);
	return true;
}

bool RenderPipelineRunner::PresentViewToBackBuffer(
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
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, MaterialPassKind::Blit);
	if (!passBinding) {
		passBinding = FindPass(*materialAsset, MaterialPassKind::Fullscreen);
	}
	// 無効なパスは処理しない
	if (!passBinding || passBinding->preferredVariant == PipelineVariantKind::Compute ||
		passBinding->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	// バックバッファのフォーマットに合わせたパイプラインステートを取得
	std::vector<DXGI_FORMAT> rtvFormats = {
		graphicsCore.GetBackBufferRenderTarget().format
	};
	const PipelineState* pipelineState = pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(),
		renderAssetLibrary_, passBinding->pipeline, passBinding->preferredVariant, rtvFormats, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetGraphicsPipeline(BlendMode::Normal)) {
		return false;
	}

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

	// バックバッファ全体をレンダーターゲットとしてバインド
	const RenderTarget& backBuffer = graphicsCore.GetBackBufferRenderTarget();
	dxCommand->BindRenderTargets(std::optional<RenderTarget>(backBuffer), std::nullopt);
	dxCommand->SetViewportAndScissor(backBuffer.width, backBuffer.height);

	// 全画面三角形を描画
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);

	return true;
}

void RenderPipelineRunner::SyncRequestedSurfaces(
	GraphicsCore& graphicsCore, const RenderFrameRequest& request) {


	for (const auto& viewRequest : request.views) {
		if (!viewRequest.enabled) {
			continue;
		}
		viewportRenderService_->SyncSurface(graphicsCore, viewRequest.kind, viewRequest.width, viewRequest.height);
	}
}

void RenderPipelineRunner::ResolveViews(const RenderFrameRequest& request) {

	gameViewState_.view = {};
	sceneViewState_.view = {};
	for (const auto& viewRequest : request.views) {

		ResolvedRenderView resolved = RenderViewResolver::Resolve(viewRequest, *request.world);
		switch (viewRequest.kind) {
		case RenderViewKind::Game:

			gameViewState_.view = resolved;
			break;
		case RenderViewKind::Scene:

			sceneViewState_.view = resolved;
			break;
		}
	}
}

SceneExecutionContext RenderPipelineRunner::BuildViewExecutionContext(GraphicsCore& graphicsCore,
	const RenderFrameRequest& request, const SceneInstance* sceneInstance,
	RenderViewKind kind, const ResolvedRenderView& view) {


	// コンテキストの構築
	SceneExecutionContext context{};
	context.kind = kind;
	context.sceneInstance = sceneInstance;
	context.view = &view;
	// SceneViewの描画カメラは変えず、設定に応じてカリングカメラだけを切り替える
	const bool useGameViewCameraForSceneCulling = graphicsCore.GetDXObject()
		.GetFeatureController().ShouldUseGameViewCameraForSceneCulling();
	context.cullingView = (kind == RenderViewKind::Scene &&
		useGameViewCameraForSceneCulling && gameViewState_.view.valid) ?
		&gameViewState_.view : &view;
	context.defaultSurface = viewportRenderService_->GetSurface(kind);
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;
	context.drawSceneViewDefaultGrid = request.drawSceneViewDefaultGrid;
	context.allowSceneComponentOverlay = (kind == RenderViewKind::Scene);
	// 種類に応じたターゲットレジストリを選択
	RenderTargetRegistry* registry = kind == RenderViewKind::Game ?
		&gameViewState_.targetRegistry : &sceneViewState_.targetRegistry;
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

	// ビューごとの中間レンダーターゲットを確保してコンテキストに設定
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewState_.resources : sceneViewState_.resources;
	resources.Resize(graphicsCore, view.width, view.height);
	context.resources = &resources;
	// ビルボードはGameViewを基準にする
	context.billboardView = (kind == RenderViewKind::Scene && gameViewState_.view.valid) ? &gameViewState_.view : &view;

	// 中間RenderTargetをレジストリに登録してPostProcessExecutorが名前で解決できるようにする
	if (resources.GetSceneMain()) {
		registry->Register("SceneMain", resources.GetSceneMain(),
			{ RenderTargetNames::kSceneColorMain, RenderTargetNames::kSceneNormalMain, RenderTargetNames::kScenePositionMain,
			  RenderTargetNames::kSceneMaterialMain, RenderTargetNames::kSceneEmissiveMain, RenderTargetNames::kSceneFlagsMain },
			std::string(RenderTargetNames::kSceneDepth));
	}
	if (resources.GetSceneFinal()) {
		registry->Register("SceneFinal", resources.GetSceneFinal(), { RenderTargetNames::kSceneColorFinal }, std::nullopt);
	}

	// ビューごとのライトGPUバッファを登録
	switch (kind) {
	case RenderViewKind::Game:

		gameViewState_.lightBuffers.RegisterTo(context.bufferRegistry);
		gameViewState_.raytracingBuffers.RegisterTo(context.bufferRegistry);
		break;
	case RenderViewKind::Scene:

		sceneViewState_.lightBuffers.RegisterTo(context.bufferRegistry);
		sceneViewState_.raytracingBuffers.RegisterTo(context.bufferRegistry);
		break;
	}
	return context;
}
