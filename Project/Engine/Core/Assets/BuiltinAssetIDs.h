#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine::BuiltinAssets {

	//============================================================================
	//	Builtin asset GUIDs
	// 移動に強い参照にするため、エンジン内蔵アセットは.metaのGUIDで固定参照する
	//============================================================================
	namespace Materials {

		inline constexpr AssetID BloomPrefilter{ 0x4e454d4153534554ull, 0x424c4f4f4d040001ull };
		inline constexpr AssetID BloomDownsample{ 0x4e454d4153534554ull, 0x424c4f4f4d040002ull };
		inline constexpr AssetID BloomUpsample{ 0x4e454d4153534554ull, 0x424c4f4f4d040003ull };
		inline constexpr AssetID BloomComposite{ 0x4e454d4153534554ull, 0x424c4f4f4d040004ull };

		inline constexpr AssetID DefaultSprite{ 0x4e454d4153534554ull, 0xef1a0db1d6ef57acull };
		inline constexpr AssetID DefaultText{ 0x4e454d4153534554ull, 0x39346ff93864d791ull };
		inline constexpr AssetID DefaultMesh{ 0x4e454d4153534554ull, 0x317efcbf7aca8e61ull };
		inline constexpr AssetID DefaultMeshOutline{ 0x4e454d4153534554ull, 0x9ed9dab27dc8ba93ull };
		inline constexpr AssetID DefaultLine{ 0x4e454d4153534554ull, 0x7a9d3c5e1b6f4084ull };
		inline constexpr AssetID DefaultPrimitive{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f606ull };
		// Plane/Ringを2D描画するときの既定マテリアル
		inline constexpr AssetID DefaultPrimitive2D{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f616ull };
		// UIProgressの進捗表示用マテリアル
		inline constexpr AssetID ProgressPrimitive{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f626ull };
		inline constexpr AssetID FullscreenCopy{ 0x4e454d4153534554ull, 0x93946a52e2bc7030ull };
		inline constexpr AssetID ToneMapToView{ 0x4e454d4153534554ull, 0xaa28624401e1e4b6ull };
		inline constexpr AssetID OutputTransform{ 0x4e454d4153534554ull, 0xae00000000000014ull };
		inline constexpr AssetID RaytracingReflection{ 0x4e454d4153534554ull, 0x375384bc3bf6bb7cull };
		inline constexpr AssetID ReflectionSpatial{ 0x4e454d4153534554ull, 0x375384bc3bf6bb7dull };
		inline constexpr AssetID ReflectionTemporal{ 0x4e454d4153534554ull, 0x375384bc3bf6bb7eull };
		inline constexpr AssetID AmbientOcclusion{ 0x4e454d4153534554ull, 0x414f000000000004ull };
		inline constexpr AssetID AmbientOcclusionFilter{ 0x4e454d4153534554ull, 0x414f000000000014ull };
		inline constexpr AssetID ReflectionComposite{ 0x4e454d4153534554ull, 0x375384bc3bf6bb7full };
		inline constexpr AssetID PostProcessMaskComposite{ 0x4e454d4153534554ull, 0x50504d41534b0004ull };
		inline constexpr AssetID ScreenSpaceOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6031ull };
		inline constexpr AssetID ScreenSpaceOutlineDilate{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6032ull };
		inline constexpr AssetID ScreenSpaceOutlineComposite{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6033ull };
		// SpriteのAlpha形状を出力するアウトラインマスク
		inline constexpr AssetID SpriteOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6043ull };
		// Primitive2DのAlpha形状を出力するアウトラインマスク
		inline constexpr AssetID Primitive2DOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6047ull };
		// Primitiveの選択アウトライン用マスクマテリアル
		inline constexpr AssetID PrimitiveOutlineMask{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f610ull };
		// パーティクルの既定マテリアル
		inline constexpr AssetID DefaultParticle{ 0x4e454d4153534554ull, 0xeff0000000000006ull };
		// 2Dパーティクルの既定マテリアル
		inline constexpr AssetID DefaultParticle2D{ 0x4e454d4153534554ull, 0xeff0000000000008ull };
	}

	namespace Textures {

		inline constexpr AssetID RectLTCMatrix{ 0x4e454d4153534554ull, 0x4c54430000000001ull };
		inline constexpr AssetID RectLTCAmplitude{ 0x4e454d4153534554ull, 0x4c54430000000002ull };
	}


	namespace RenderPasses {

		inline constexpr AssetID Default{
			0x4e454d4153534554ull, 0x525450524f46494cull };
	}

	namespace Pipelines {

		inline constexpr AssetID AutoExposure{ 0x4e454d4153534554ull, 0xae00000000000003ull };
		inline constexpr AssetID DefaultMeshZPrepass{ 0x4e454d4153534554ull, 0xf09836087840b1d2ull };
		inline constexpr AssetID DefaultMeshEditorPicking{ 0x4e454d4153534554ull, 0x8d50435034671c2cull };
		inline constexpr AssetID DefaultMesh{ 0x4e454d4153534554ull, 0x966f3e8a34595313ull };
		inline constexpr AssetID DefaultMeshMasked{ 0x4e454d4153534554ull, 0x966f3e8a34595314ull };
		inline constexpr AssetID DefaultMeshTransparent{ 0x4e454d4153534554ull, 0xb8996e4516e31236ull };
		inline constexpr AssetID DefaultSprite{ 0x4e454d4153534554ull, 0x3309336c2fa4669cull };
		inline constexpr AssetID DefaultText{ 0x4e454d4153534554ull, 0xbe890ab815b3de9aull };
		inline constexpr AssetID DefaultPrimitive{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f605ull };
		inline constexpr AssetID DefaultPrimitiveTransparent{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f609ull };
		inline constexpr AssetID DefaultPrimitiveEditorPicking{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f619ull };
		inline constexpr AssetID DefaultPrimitive2D{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f615ull };
		inline constexpr AssetID DefaultLine{ 0x4e454d4153534554ull, 0x7a9d3c5e1b6f4083ull };
		inline constexpr AssetID Skinning{ 0x4e454d4153534554ull, 0xda1205f1e1e19bdbull };
		inline constexpr AssetID BuildIndexedIndirectArgs{ 0x4e454d4153534554ull, 0xe0400afbd444f5d6ull };
		inline constexpr AssetID InclusiveScan{ 0x4e454d4153534554ull, 0x7363616e00000003ull };
		inline constexpr AssetID BuildDepthPyramid{ 0x4e454d4153534554ull, 0xd9a171e5f80901a3ull };
		// パーティクルの形状アニメ用MSパイプラインとトレイル用パイプライン
		inline constexpr AssetID DefaultParticle{ 0x4e454d4153534554ull, 0xeff0000000000005ull };
		inline constexpr AssetID ParticleRingMS{ 0x4e454d4153534554ull, 0xeff000000000000bull };
		inline constexpr AssetID ParticleCylinderMS{ 0x4e454d4153534554ull, 0xeff000000000000eull };
		inline constexpr AssetID ParticleMeshSampling{ 0x4e454d4153534554ull, 0x7061727400030003ull };
		inline constexpr AssetID ParticleGPUExtendedSimulate{ 0x4e454d4153534554ull, 0x7061727400030023ull };
		inline constexpr AssetID ParticleGPUEventPrepare{ 0x4e454d4153534554ull, 0x7061727400000340ull };
		inline constexpr AssetID ParticleGPUPaint{ 0x4e454d4153534554ull, 0x7061727400000341ull };
		inline constexpr AssetID ParticleGPUImpactDecal{ 0x4e454d4153534554ull, 0x7061727400000342ull };
		inline constexpr AssetID ParticleGPUCurlSimulate{ 0x4e454d4153534554ull, 0x7061727400030013ull };
		inline constexpr AssetID ParticleGPUSimulate{ 0x4e454d4153534554ull, 0x706172740000000bull };
		inline constexpr AssetID ParticleGPUView{ 0x4e454d4153534554ull, 0x706172740000000dull };
		inline constexpr AssetID ParticleGPUDraw{ 0x4e454d4153534554ull, 0x706172740000000full };
		inline constexpr AssetID ParticleGPUDraw2D{ 0x4e454d4153534554ull, 0x7061727400000010ull };
		inline constexpr AssetID ParticleGPUTrailUpdate{ 0x4e454d4153534554ull, 0x7061727400000111ull };
		inline constexpr AssetID ParticleGPUTrailPrepare{ 0x4e454d4153534554ull, 0x7061727400000113ull };
		inline constexpr AssetID ParticleGPUTrailDraw{ 0x4e454d4153534554ull, 0x7061727400000115ull };
		inline constexpr AssetID ParticleGPUTrailDraw2D{ 0x4e454d4153534554ull, 0x7061727400000116ull };
		inline constexpr AssetID ParticleGPURibbonSelect{ 0x4e454d4153534554ull, 0x7061727400000118ull };
		inline constexpr AssetID ParticleTrail{ 0x4e454d4153534554ull, 0xeff0000000000012ull };
		inline constexpr AssetID ParticleTrail2D{ 0x4e454d4153534554ull, 0x7061727400000120ull };
		inline constexpr AssetID ScreenSpaceOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6021ull };
		inline constexpr AssetID SpriteOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6042ull };
		inline constexpr AssetID Primitive2DOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6046ull };
		inline constexpr AssetID ScreenSpaceOutlineComposite{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6023ull };
		inline constexpr AssetID PostProcessMaskComposite{ 0x4e454d4153534554ull, 0x50504d41534b0003ull };
		inline constexpr AssetID RaytracingReflection{ 0x4e454d4153534554ull, 0x6228d203578ecde2ull };
		inline constexpr AssetID ReflectionSpatial{ 0x4e454d4153534554ull, 0x6228d203578ecde3ull };
		inline constexpr AssetID ReflectionTemporal{ 0x4e454d4153534554ull, 0x6228d203578ecde4ull };
		inline constexpr AssetID ReflectionComposite{ 0x4e454d4153534554ull, 0x6228d203578ecde5ull };
	}

	namespace Shaders {

		inline constexpr AssetID ParticleGPUImpactDecalDraw{ 0x4e454d4153534554ull, 0x7061727400000333ull };
		inline constexpr AssetID Decal{ 0x4e454d4153534554ull, 0x444543414c000001ull };
		inline constexpr AssetID FullscreenCopy{ 0x4e454d4153534554ull, 0x8ed3590f7d583b7aull };
		inline constexpr AssetID DeferredLighting{ 0x4e454d4153534554ull, 0x4c49474854000001ull };
		inline constexpr AssetID DeferredLightingShadowed{ 0x4e454d4153534554ull, 0x4c49474854000002ull };
		inline constexpr AssetID SkyboxIrradiance{ 0x4e454d4153534554ull, 0x4c49474854000003ull };
		inline constexpr AssetID EnvironmentSpecular{ 0x4e454d4153534554ull, 0x454e5649524f0001ull };
		inline constexpr AssetID EnvironmentBRDF{ 0x4e454d4153534554ull, 0x454e5649524f0002ull };
		inline constexpr AssetID DepthVisualize{ 0x4e454d4153534554ull, 0x4c49474854000004ull };
		inline constexpr AssetID Skybox{ 0x4e454d4153534554ull, 0x4c49474854000005ull };
		inline constexpr AssetID InlineShadowTrace{ 0x4e454d4153534554ull, 0x5348445700000001ull };
		inline constexpr AssetID InlineShadowTemporal{ 0x4e454d4153534554ull, 0x5348445700000002ull };
		inline constexpr AssetID InlineShadowSpatial{ 0x4e454d4153534554ull, 0x5348445700000003ull };
		// 固定描画パスが製品実行で必要とするシェーダー
		inline constexpr AssetID FixedRuntime[] = {
			Decal,
			FullscreenCopy, DeferredLighting, DeferredLightingShadowed,
			SkyboxIrradiance, EnvironmentSpecular, EnvironmentBRDF, DepthVisualize, Skybox,
			InlineShadowTrace, InlineShadowTemporal, InlineShadowSpatial,
		};

		inline constexpr AssetID MeshGeometryVS{ 0x4e454d4153534554ull, 0xf6485468f3b0f105ull };
		inline constexpr AssetID MeshGeometryAS{ 0x4e454d4153534554ull, 0x06811f98f605bb52ull };
		inline constexpr AssetID MeshGeometryMS{ 0x4e454d4153534554ull, 0x109772a2d84dd757ull };
		// VSと既定PSを持つパーティクルの基本シェーダー
		inline constexpr AssetID Particle{ 0x4e454d4153534554ull, 0xeff0000000000004ull };
	}

	// 製品実行時に直接参照されるアセットは動的生成の有無にかかわらず同梱する
	namespace Runtime {

		inline constexpr AssetID Assets[] = {
			Shaders::Decal, Shaders::ParticleGPUImpactDecalDraw,
			Materials::DefaultSprite, Materials::DefaultText, Materials::DefaultMesh,
			Materials::DefaultMeshOutline, Materials::DefaultLine, Materials::DefaultPrimitive,
			Materials::DefaultPrimitive2D, Materials::ProgressPrimitive, Materials::FullscreenCopy,
			Materials::BloomPrefilter, Materials::BloomDownsample, Materials::BloomUpsample, Materials::BloomComposite,
			Materials::ToneMapToView, Materials::OutputTransform, Materials::RaytracingReflection,
			Materials::ReflectionSpatial, Materials::ReflectionTemporal, Materials::ReflectionComposite,
			Materials::AmbientOcclusion, Materials::AmbientOcclusionFilter,
			Materials::PostProcessMaskComposite, Materials::ScreenSpaceOutlineMask,
			Materials::ScreenSpaceOutlineDilate, Materials::ScreenSpaceOutlineComposite,
			Materials::SpriteOutlineMask, Materials::Primitive2DOutlineMask, Materials::PrimitiveOutlineMask,
			Materials::DefaultParticle, Materials::DefaultParticle2D,
			Pipelines::AutoExposure, Pipelines::Skinning, Pipelines::BuildIndexedIndirectArgs,
			Pipelines::BuildDepthPyramid, Pipelines::ParticleRingMS, Pipelines::ParticleCylinderMS,
			Pipelines::ParticleGPUExtendedSimulate, Pipelines::ParticleGPUEventPrepare, Pipelines::ParticleGPUPaint, Pipelines::ParticleGPUImpactDecal,
			Pipelines::ParticleMeshSampling, Pipelines::ParticleGPUCurlSimulate, Pipelines::InclusiveScan, Pipelines::ParticleTrail, Pipelines::ParticleTrail2D, Pipelines::ParticleGPUSimulate, Pipelines::ParticleGPUView,
			Pipelines::ParticleGPUDraw, Pipelines::ParticleGPUDraw2D,
			Pipelines::ParticleGPUTrailUpdate, Pipelines::ParticleGPUTrailPrepare, Pipelines::ParticleGPURibbonSelect,
			Pipelines::ParticleGPUTrailDraw, Pipelines::ParticleGPUTrailDraw2D, RenderPasses::Default, Textures::RectLTCMatrix, Textures::RectLTCAmplitude,
			Shaders::InlineShadowTrace, Shaders::InlineShadowTemporal, Shaders::InlineShadowSpatial,
		};
	}

	namespace EditorTextures {

		inline constexpr AssetID DirectionalLightIcon{ 0x4e454d4153534554ull, 0xa0e16f0b78050650ull };
		inline constexpr AssetID PointLightIcon{ 0x4e454d4153534554ull, 0x235683763b017d5full };
		inline constexpr AssetID SpotLightIcon{ 0x4e454d4153534554ull, 0xc6fc55af5732810eull };
		inline constexpr AssetID PerspectiveCameraIcon{ 0x4e454d4153534554ull, 0x3dca1ce7a4f02931ull };
	}
}
