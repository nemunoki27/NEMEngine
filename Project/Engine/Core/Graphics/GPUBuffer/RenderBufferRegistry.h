#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxStructures.h>

// c++
#include <string>
#include <unordered_map>
#include <vector>
// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	RenderBufferRegistry structures
	//============================================================================

	// 登録された描画バッファの情報
	struct RegisteredRenderBuffer {

		// 登録名
		std::string alias;

		// GPUリソース
		ID3D12Resource* resource = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		// GPUディスクリプタハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE srvGPUHandle{};
		D3D12_GPU_DESCRIPTOR_HANDLE uavGPUHandle{};

		uint32_t elementCount = 0;
		uint32_t stride = 0;
	};

	//============================================================================
	//	RenderBufferRegistry class
	//	描画に使用するバッファの管理クラス
	//============================================================================
	class RenderBufferRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderBufferRegistry() = default;
		~RenderBufferRegistry() = default;

		// 登録
		void Register(const RegisteredRenderBuffer& entry);

		// データクリア
		void Clear();

		//--------- accessor -----------------------------------------------------

		RegisteredRenderBuffer* Find(const std::string& alias);
		const RegisteredRenderBuffer* Find(const std::string& alias) const;

		// 登録された描画バッファ配列をそのまま参照する
		const std::vector<RegisteredRenderBuffer>& GetEntries() const { return entries_; }

		// 登録数取得
		size_t GetCount() const { return entries_.size(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 登録された描画バッファのリスト
		std::vector<RegisteredRenderBuffer> entries_;
		// 登録名と描画バッファのインデックスのマップ
		std::unordered_map<std::string, size_t> aliasTable_;
	};
} // Engine