#include "RuntimeRenderPreloader.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Registry/RenderBackendRegistry.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/RenderPathResources.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

using namespace Engine;

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

void Engine::RuntimeRenderPreloader::Preload(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
	RuntimeRenderPreloadContext& context) {

	Logger::Output(LogType::Engine, "[実行時事前読み込み] 開始");
	context.assetLibrary.Init(&assetDatabase);
	context.assetGenerator.EnsureBuiltinAssets(&assetDatabase);

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
	std::vector<AssetID> renderFeatureProfiles{};
	TextureUploadService& textureUploadService = graphicsCore.GetTextureUploadService();
	for (const AssetMeta* meta : assets) {

		switch (meta->type) {
		case AssetType::Texture:
		{
			// Materialの両用途を先読みする、明示色空間なら同じキャッシュへ統合される
			RuntimeTextureResolver::Resolve(graphicsCore, &assetDatabase,
				meta->guid, TextureColorSpace::Linear);
			RuntimeTextureResolver::Resolve(graphicsCore, &assetDatabase,
				meta->guid, TextureColorSpace::SRGB);
			break;
		}
		case AssetType::Material:
			context.assetLibrary.LoadMaterial(meta->guid);
			materialAssets.emplace_back(meta->guid);
			break;
		case AssetType::Shader:
		{
			const std::string path = Algorithm::ToLower(meta->assetPath);
			if (Algorithm::EndsWith(path, ".shader.json") ||
				Algorithm::EndsWith(path, ".shader")) {
				context.assetLibrary.LoadShader(meta->guid);
			}
			break;
		}
		case AssetType::RenderPipeline:
			context.assetLibrary.LoadPipeline(meta->guid);
			pipelineAssets.emplace_back(meta->guid);
			break;
		case AssetType::Font:
		{
			const std::string path = Algorithm::ToLower(meta->assetPath);
			if (Algorithm::EndsWith(path, ".font.json") ||
				Algorithm::EndsWith(path, ".msdf.json") ||
				Algorithm::EndsWith(path, ".font")) {
				context.assetLibrary.LoadFont(meta->guid);
			}
			break;
		}
		case AssetType::ParticleEffect:
			context.assetLibrary.LoadParticleEffect(meta->guid);
			break;
		case AssetType::Mesh:
			meshAssets.emplace_back(meta->guid);
			break;
		case AssetType::RenderFeatureProfile:
			context.assetLibrary.LoadRenderFeatureProfile(meta->guid);
			renderFeatureProfiles.emplace_back(meta->guid);
			break;
		default:
			break;
		}
	}

	// 全テクスチャのCPUデコードとGPU転送を完了する
	textureUploadService.WaitAll();

	// 通常メッシュとModel Particleは別キャッシュなので両方作成する
	context.backends.BeginFrame(graphicsCore);
	if (context.meshBackend) {
		context.meshBackend->PreloadMeshes(graphicsCore, assetDatabase, meshAssets);
	}
	if (context.particleBackend) {
		context.particleBackend->PreloadMeshes(graphicsCore, assetDatabase, meshAssets);
	}
	graphicsCore.GetBufferUploadService().FlushAndWait();

	// 初回描画で解像度依存のRTを作らないように先に確保する
	const auto& windowSetting = graphicsCore.GetContext().GetWindowSetting();
	const uint32_t width = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.x));
	const uint32_t height = static_cast<uint32_t>((std::max)(1, windowSetting.gameSize.y));
	context.viewport.SyncSurface(graphicsCore, RenderViewKind::Game, width, height);
	context.gameResources.Resize(graphicsCore, width, height);

	const GraphicsRuntimeFeatures& runtimeFeatures =
		graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const DXGI_FORMAT backBufferFormat = graphicsCore.GetBackBufferRenderTarget().format;
	size_t pipelineCount = 0;
	auto preloadPipeline = [&](const MaterialAsset& material, const MaterialPassBinding& pass,
		const RuntimeTargetFormats& formats, bool forceDepthTestWrite = false) {

		const PipelineState* pipeline = nullptr;
		if (pass.preferredVariant == PipelineVariantKind::Raytracing) {
			pipeline = nullptr;
			context.raytracingPipelines.GetOrCreate(
				graphicsCore.GetDXObject(), context.assetLibrary, pass.pipeline);
		} else if (pass.preferredVariant == PipelineVariantKind::Compute) {

			PipelineStaticSamplerOverrideSet samplerOverrides{};
			samplerOverrides.fillMissingSamplers = true;
			pipeline = context.pipelines.GetORCreate(graphicsCore.GetDXObject(), context.assetLibrary,
				pass.pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN,
				runtimeFeatures, nullptr, false, &samplerOverrides,
				pass.shaderOverride);
		} else if (pass.shaderOverride) {
			pipeline = context.pipelines.GetORCreateComposed(graphicsCore.GetDXObject(), context.assetLibrary,
				pass.pipeline, pass.pipeline, pass.shaderOverride, pass.preferredVariant,
				formats.rtvFormats, formats.dsvFormat, runtimeFeatures);
		} else {
			pipeline = context.pipelines.GetORCreate(graphicsCore.GetDXObject(), context.assetLibrary,
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

				if (context.pipelines.GetORCreateComposed(graphicsCore.GetDXObject(), context.assetLibrary,
					pass.pipeline, geometryPipeline, pass.shaderOverride, PipelineVariantKind::GraphicsMesh,
					formats.rtvFormats, formats.dsvFormat, runtimeFeatures)) {
					++pipelineCount;
				}
			}
			const PipelineVariantKind trailKind = runtimeFeatures.useMeshShader ?
				PipelineVariantKind::GraphicsMesh : PipelineVariantKind::GraphicsVertex;
			if (context.pipelines.GetORCreateComposed(graphicsCore.GetDXObject(), context.assetLibrary,
				pass.pipeline, BuiltinAssets::Pipelines::ParticleTrail, pass.shaderOverride, trailKind,
				formats.rtvFormats, formats.dsvFormat, runtimeFeatures)) {
				++pipelineCount;
			}
		}
	};

	for (AssetID materialID : materialAssets) {

		const MaterialAsset* material = context.assetLibrary.LoadMaterial(materialID);
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

		const RenderPipelineAsset* pipelineAsset = context.assetLibrary.LoadPipeline(pipelineID);
		if (!pipelineAsset) {
			continue;
		}
		for (const PipelineVariantDesc& variant : pipelineAsset->variants) {

			if (variant.kind == PipelineVariantKind::Raytracing) {
				if (context.raytracingPipelines.GetOrCreate(
					graphicsCore.GetDXObject(), context.assetLibrary, pipelineID)) {
					++pipelineCount;
				}
				continue;
			}
			if (variant.kind == PipelineVariantKind::Compute) {

				if (context.pipelines.GetORCreate(graphicsCore.GetDXObject(), context.assetLibrary,
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
			if (context.pipelines.GetORCreate(graphicsCore.GetDXObject(), context.assetLibrary,
				pipelineID, variant.kind, formats.rtvFormats, formats.dsvFormat, runtimeFeatures)) {
				++pipelineCount;
			}
		}
	}

	// Profile単位のSampler上書きを含めてCompute/DXRを事前作成する
	for (AssetID profileID : renderFeatureProfiles) {

		const RenderFeatureProfileAsset* profile =
			context.assetLibrary.LoadRenderFeatureProfile(profileID);
		if (!profile) {
			continue;
		}
		for (const RenderFeaturePassSettings& featurePass : profile->passes) {

			const MaterialAsset* material =
				context.assetLibrary.LoadMaterial(featurePass.material);
			const MaterialPassBinding* materialPass = material ?
				FindPass(*material, featurePass.materialPass) : nullptr;
			if (!materialPass) {
				continue;
			}
			if (featurePass.type == RenderFeaturePassType::RayTracing) {
				if (context.raytracingPipelines.GetOrCreate(
					graphicsCore.GetDXObject(), context.assetLibrary,
					materialPass->pipeline,
					materialPass->shaderOverride)) {

					++pipelineCount;
				}
				continue;
			}
			if (materialPass->preferredVariant != PipelineVariantKind::Compute) {
				continue;
			}
			PipelineStaticSamplerOverrideSet samplerOverrides{};
			samplerOverrides.fillMissingSamplers = true;
			samplerOverrides.byName = featurePass.samplerOverrides;
			if (context.pipelines.GetORCreate(graphicsCore.GetDXObject(), context.assetLibrary,
				materialPass->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN,
				runtimeFeatures, nullptr, false, &samplerOverrides,
				materialPass->shaderOverride)) {
				++pipelineCount;
			}
		}
	}

	Logger::Output(LogType::Engine,
		"[実行時事前読み込み] アセット={} テクスチャ={} メッシュ={} マテリアル={} パイプライン={}",
		assets.size(),
		static_cast<size_t>(std::count_if(assets.begin(), assets.end(), [](const AssetMeta* meta) {
			return meta->type == AssetType::Texture;
			})),
		meshAssets.size(), materialAssets.size(), pipelineCount);
}
