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

		inline constexpr AssetID DefaultSprite{ 0x4e454d4153534554ull, 0xef1a0db1d6ef57acull };
		inline constexpr AssetID DefaultText{ 0x4e454d4153534554ull, 0x39346ff93864d791ull };
		inline constexpr AssetID DefaultMesh{ 0x4e454d4153534554ull, 0x317efcbf7aca8e61ull };
		inline constexpr AssetID DefaultMeshOutline{ 0x4e454d4153534554ull, 0x9ed9dab27dc8ba93ull };
		inline constexpr AssetID DefaultLine{ 0x4e454d4153534554ull, 0x7a9d3c5e1b6f4084ull };
		inline constexpr AssetID DefaultFillMesh{ 0x4e454d4153534554ull, 0x4b5a225df5b0044bull };
		inline constexpr AssetID DefaultPrimitive{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f606ull };
		// Plane/Ringを2D描画するときの既定マテリアル
		inline constexpr AssetID DefaultPrimitive2D{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f616ull };
		// UIProgressの進捗表示用マテリアル
		inline constexpr AssetID ProgressPrimitive{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f626ull };
		// アイリス遷移用マテリアル
		inline constexpr AssetID IrisTransition{ 0x4e454d4153534554ull, 0x1a15000000000004ull };
		inline constexpr AssetID FullscreenCopy{ 0x4e454d4153534554ull, 0x93946a52e2bc7030ull };
		inline constexpr AssetID ToneMapToView{ 0x4e454d4153534554ull, 0xaa28624401e1e4b6ull };
		inline constexpr AssetID OutputTransform{ 0x4e454d4153534554ull, 0xae00000000000014ull };
		inline constexpr AssetID RaytracingReflection{ 0x4e454d4153534554ull, 0x375384bc3bf6bb7cull };
		inline constexpr AssetID PostProcessMaskComposite{ 0x4e454d4153534554ull, 0x50504d41534b0004ull };
		inline constexpr AssetID ScreenSpaceOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6031ull };
		inline constexpr AssetID ScreenSpaceOutlineDilate{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6032ull };
		inline constexpr AssetID ScreenSpaceOutlineComposite{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6033ull };
		// Primitive/FillMeshの選択アウトライン用マスクマテリアル
		inline constexpr AssetID PrimitiveOutlineMask{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f610ull };
		inline constexpr AssetID FillMeshOutlineMask{ 0x4e454d4153534554ull, 0xfa11e50000000a07ull };
		// パーティクルの既定マテリアル
		inline constexpr AssetID DefaultParticle{ 0x4e454d4153534554ull, 0xeff0000000000006ull };
		// 2Dパーティクルの既定マテリアル
		inline constexpr AssetID DefaultParticle2D{ 0x4e454d4153534554ull, 0xeff0000000000008ull };
	}

	namespace Effects {

		// パーティクルの既定エフェクト
		inline constexpr AssetID DefaultParticle{ 0x4e454d4153534554ull, 0xeff0000000000010ull };
	}

	namespace Pipelines {

		inline constexpr AssetID AutoExposure{ 0x4e454d4153534554ull, 0xae00000000000003ull };
		inline constexpr AssetID DefaultMeshZPrepass{ 0x4e454d4153534554ull, 0xf09836087840b1d2ull };
		inline constexpr AssetID DefaultMeshEditorPicking{ 0x4e454d4153534554ull, 0x8d50435034671c2cull };
		inline constexpr AssetID DefaultMesh{ 0x4e454d4153534554ull, 0x966f3e8a34595313ull };
		inline constexpr AssetID DefaultMeshTransparent{ 0x4e454d4153534554ull, 0xb8996e4516e31236ull };
		inline constexpr AssetID DefaultSprite{ 0x4e454d4153534554ull, 0x3309336c2fa4669cull };
		inline constexpr AssetID DefaultText{ 0x4e454d4153534554ull, 0xbe890ab815b3de9aull };
		inline constexpr AssetID DefaultPrimitive{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f605ull };
		inline constexpr AssetID DefaultPrimitiveTransparent{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f609ull };
		inline constexpr AssetID DefaultPrimitive2D{ 0x4e454d4153534554ull, 0x70a1b2c3d4e5f615ull };
		inline constexpr AssetID DefaultFillMesh{ 0x4e454d4153534554ull, 0xe477347a287dcf1dull };
		inline constexpr AssetID DefaultFillMeshTransparent{ 0x4e454d4153534554ull, 0x8e7b6e92f0d1c3a5ull };
		inline constexpr AssetID DefaultLine{ 0x4e454d4153534554ull, 0x7a9d3c5e1b6f4083ull };
		inline constexpr AssetID Skinning{ 0x4e454d4153534554ull, 0xda1205f1e1e19bdbull };
		inline constexpr AssetID BuildIndexedIndirectArgs{ 0x4e454d4153534554ull, 0xe0400afbd444f5d6ull };
		inline constexpr AssetID BuildDepthPyramid{ 0x4e454d4153534554ull, 0xd9a171e5f80901a3ull };
		// パーティクルの形状アニメ用MSパイプラインとトレイル用パイプライン
		inline constexpr AssetID DefaultParticle{ 0x4e454d4153534554ull, 0xeff0000000000005ull };
		inline constexpr AssetID ParticleRingMS{ 0x4e454d4153534554ull, 0xeff000000000000bull };
		inline constexpr AssetID ParticleCylinderMS{ 0x4e454d4153534554ull, 0xeff000000000000eull };
		inline constexpr AssetID ParticleTrail{ 0x4e454d4153534554ull, 0xeff0000000000012ull };
		inline constexpr AssetID ScreenSpaceOutlineMask{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6021ull };
		inline constexpr AssetID ScreenSpaceOutlineComposite{ 0x4e454d4153534554ull, 0x7c1d9a4b8e2f6023ull };
		inline constexpr AssetID PostProcessMaskComposite{ 0x4e454d4153534554ull, 0x50504d41534b0003ull };
	}

	namespace Shaders {

		inline constexpr AssetID MeshGeometryVS{ 0x4e454d4153534554ull, 0xf6485468f3b0f105ull };
		inline constexpr AssetID MeshGeometryAS{ 0x4e454d4153534554ull, 0x06811f98f605bb52ull };
		inline constexpr AssetID MeshGeometryMS{ 0x4e454d4153534554ull, 0x109772a2d84dd757ull };
		// VSと既定PSを持つパーティクルの基本シェーダー
		inline constexpr AssetID Particle{ 0x4e454d4153534554ull, 0xeff0000000000004ull };
	}

	namespace EditorTextures {

		inline constexpr AssetID DirectionalLightIcon{ 0x4e454d4153534554ull, 0xa0e16f0b78050650ull };
		inline constexpr AssetID PointLightIcon{ 0x4e454d4153534554ull, 0x235683763b017d5full };
		inline constexpr AssetID SpotLightIcon{ 0x4e454d4153534554ull, 0xc6fc55af5732810eull };
		inline constexpr AssetID PerspectiveCameraIcon{ 0x4e454d4153534554ull, 0x3dca1ce7a4f02931ull };
	}
}
