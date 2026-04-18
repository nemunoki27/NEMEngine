#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// c++
#include <cstdint>
#include <string>
// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	GPUTextureResource structure
	//============================================================================
	
	// GPUのテクスチャリソースを表す構造体
	struct GPUTextureResource {

		ComPtr<ID3D12Resource> resource;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
		uint32_t srvIndex = UINT32_MAX;

		// テクスチャ名
		std::string textureName;

		// リソースが有効かどうか
		bool valid = false;
	};
}