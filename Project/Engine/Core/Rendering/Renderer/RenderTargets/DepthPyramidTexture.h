#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <cstdint>
#include <string_view>
#include <vector>
// directX
#include <d3d12.h>

namespace Engine {

	// front
	class DxCommand;
	class SRVDescriptor;

	//============================================================================
	//	DepthPyramidTexture class
	// 深度プリパスを縮小したHi-ZテクスチャをView単位で保持する
	//============================================================================
	class DepthPyramidTexture {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		DepthPyramidTexture() = default;
		~DepthPyramidTexture();
		DepthPyramidTexture(const DepthPyramidTexture&) = delete;
		DepthPyramidTexture& operator=(const DepthPyramidTexture&) = delete;

		// サイズに合わせて全MipのSRV/UAVを生成する
		void Create(ID3D12Device* device, SRVDescriptor* srvDescriptor,
			uint32_t width, uint32_t height);
		// 破棄
		void Destroy();

		// 指定Mipだけを遷移する
		void TransitionMip(DxCommand& dxCommand, uint32_t mipIndex,
			D3D12_RESOURCE_STATES newState);

		//--------- accessor -----------------------------------------------------

		bool IsValid() const { return resource_ != nullptr; }
		ID3D12Resource* GetResource() const { return resource_.Get(); }
		uint32_t GetWidth() const { return width_; }
		uint32_t GetHeight() const { return height_; }
		uint32_t GetMipCount() const { return mipCount_; }
		void MarkBuilt(uint64_t frameSerial) { lastBuiltFrameSerial_ = frameSerial; }
		bool IsBuiltForFrame(uint64_t frameSerial) const { return lastBuiltFrameSerial_ == frameSerial; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetSRVGPUHandle() const { return srvGPUHandle_; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetMipSRVGPUHandle( uint32_t mipIndex) const { return mipSRVGPUHandles_[mipIndex]; }
		const D3D12_GPU_DESCRIPTOR_HANDLE& GetMipUAVGPUHandle( uint32_t mipIndex) const { return mipUAVGPUHandles_[mipIndex]; }
		static constexpr std::string_view kBindingName =
			"gOcclusionDepthPyramid";
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		SRVDescriptor* srvDescriptor_ = nullptr;
		ComPtr<ID3D12Resource> resource_{};
		uint32_t width_ = 0;
		uint32_t height_ = 0;
		uint32_t mipCount_ = 0;
		uint64_t lastBuiltFrameSerial_ = 0;

		uint32_t srvIndex_ = UINT32_MAX;
		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle_{};
		std::vector<uint32_t> mipSRVIndices_{};
		std::vector<uint32_t> mipUAVIndices_{};
		std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mipSRVGPUHandles_{};
		std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> mipUAVGPUHandles_{};
		std::vector<D3D12_RESOURCE_STATES> mipStates_{};

		//--------- functions ----------------------------------------------------

		static uint32_t CalculateMipCount(uint32_t width, uint32_t height);
	};
} // Engine
