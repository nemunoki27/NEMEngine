#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxStructures.h>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	BlendState class
	//	描画ターゲットごとのブレンド設定を生成し、D3D12のブレンド記述に反映する。
	//============================================================================
	class BlendState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BlendState() = default;
		~BlendState() = default;

		// 指定BlendModeに応じてD3D12_RENDER_TARGET_BLEND_DESCを組み立てる
		void Create(BlendMode blendMode, D3D12_RENDER_TARGET_BLEND_DESC& blendDesc);
	};
}; // Engine