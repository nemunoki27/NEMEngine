#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>

// directX
#include <d3d12.h>
// c++
#include <cstdint>
#include <string>
#include <array>

//============================================================================
//	DxStructures
// D3D12で使用する汎用構造体/列挙/補助関数を定義する
//============================================================================
// 描画先の情報
namespace Engine {

	struct RenderTarget {

		uint32_t width;
		uint32_t height;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
		Color4 clearColor;
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle;
	};
}; // Engine
