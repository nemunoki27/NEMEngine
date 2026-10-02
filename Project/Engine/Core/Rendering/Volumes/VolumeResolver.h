#pragma once

//============================================================================
//	include
//============================================================================
#include "VolumeProfileAsset.h"

namespace Engine {

	// front
	class ECSWorld;
	class RenderAssetLibrary;
	struct ResolvedCameraView;

	//============================================================================
	//	VolumeResolver class
	//	Camera位置とVolumeから画面効果設定を確定するクラス
	//============================================================================
	class VolumeResolver final {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// Cameraへ適用する画面効果設定を確定する
		static ColorPipelineSettings Resolve(ECSWorld& world, RenderAssetLibrary& assetLibrary,
			const ResolvedCameraView& camera);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		// Volumeの設定を重み付きで合成する
		static void Blend(ColorPipelineSettings& base, const ColorPipelineSettings& overlay, float weight);
	};
} // Engine
