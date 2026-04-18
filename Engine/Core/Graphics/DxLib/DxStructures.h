#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Color.h>

// directX
#include <d3d12.h>
// c++
#include <cstdint>
#include <string>
#include <array>

//============================================================================
//	DxStructures
//	D3D12で使用する汎用構造体/列挙/補助関数を定義する。
//============================================================================

// 描画先の情報
namespace Engine {

	struct RenderTarget {

		uint32_t width;
		uint32_t height;
		Color clearColor;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	};

	enum BlendMode {

		Normal,   // 通常αブレンド
		Add,      // 加算
		Subtract, // 減算
		Multiply, // 乗算
		Screen,   // スクリーン
	};
	static constexpr const uint32_t kBlendModeCount = static_cast<uint32_t>(BlendMode::Screen) + 1;
}; // Engine