#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <cstdint>
#include <memory>
#include <string>
// directX
#include <d3d12.h>

namespace Engine {

	using TextureOwnership = std::shared_ptr<const uint8_t>;

	//============================================================================
	//	GPUTextureResource structure
	//============================================================================
	// GPUのテクスチャリソースを表す構造体
	struct GPUTextureResource {

		ComPtr<ID3D12Resource> resource;
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
		uint32_t srvIndex = UINT32_MAX;
		uint64_t allocationBytes = 0;

		// テクスチャ名
		std::string textureName;

		// リソースが有効かどうか
		bool valid = false;
		TextureOwnership ownership;
	};
}
