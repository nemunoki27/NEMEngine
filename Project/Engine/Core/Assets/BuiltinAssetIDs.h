#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine::BuiltinAssets {

	//============================================================================
	//	Builtin asset GUIDs
	//	移動に強い参照にするため、エンジン内蔵アセットは.metaのGUIDで固定参照する。
	//============================================================================

	namespace Materials {

		inline constexpr AssetID DefaultSprite{ 0xef1a0db1d6ef57acull };
		inline constexpr AssetID DefaultText{ 0x39346ff93864d791ull };
		inline constexpr AssetID DefaultMesh{ 0xb876437b44304df0ull };
		inline constexpr AssetID DefaultMeshOutline{ 0x9ed9dab27dc8ba93ull };
		inline constexpr AssetID FullscreenCopy{ 0x93946a52e2bc7030ull };
		inline constexpr AssetID ToneMapToView{ 0xaa28624401e1e4b6ull };
		inline constexpr AssetID LightCulling{ 0xa8b3c1c806a8f747ull };
		inline constexpr AssetID RaytracingReflection{ 0x375384bc3bf6bb7cull };
		inline constexpr AssetID ScreenSpaceOutlineMask{ 0x7c1d9a4b8e2f6031ull };
		inline constexpr AssetID ScreenSpaceOutlineDilate{ 0x7c1d9a4b8e2f6032ull };
		inline constexpr AssetID ScreenSpaceOutlineComposite{ 0x7c1d9a4b8e2f6033ull };
	}

	namespace Pipelines {

		inline constexpr AssetID DefaultMeshZPrepass{ 0xf09836087840b1d2ull };
		inline constexpr AssetID DefaultMesh{ 0x966f3e8a34595313ull };
		inline constexpr AssetID Skinning{ 0xda1205f1e1e19bdbull };
		inline constexpr AssetID BuildIndexedIndirectArgs{ 0xe0400afbd444f5d6ull };
		inline constexpr AssetID ScreenSpaceOutlineMask{ 0x7c1d9a4b8e2f6021ull };
		inline constexpr AssetID ScreenSpaceOutlineComposite{ 0x7c1d9a4b8e2f6023ull };
	}
}
