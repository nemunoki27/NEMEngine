#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <cstdint>
#include <string>
#include <vector>
// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	GpuFrameProfiler class
	//	D3D12タイムスタンプクエリで描画パスごとのGPU処理時間を計測するシングルトン。
	//	エンジンは毎フレームGPU完了を待つため、前フレームの解決結果は次フレーム開始時に安全に読める。
	//============================================================================
	class GpuFrameProfiler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		static GpuFrameProfiler& GetInstance();

		// フレーム開始。初回は遅延初期化し、前フレームの計測結果をFrameProfilerへ反映してから記録をリセットする
		void BeginFrame(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
		// パス計測(BeginFrame～Resolveの間のみ有効)
		void BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name);
		void EndPass(ID3D12GraphicsCommandList* commandList);
		// 記録を締め切り、ResolveQueryDataをコマンドリストへ積む
		void Resolve(ID3D12GraphicsCommandList* commandList);

		// 終了処理
		void Finalize();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		GpuFrameProfiler() = default;

		//--------- structure ----------------------------------------------------

		// パスごとの計測範囲(タイムスタンプのインデックス)
		struct PassRecord {

			std::string name;
			uint32_t beginIndex = 0;
			uint32_t endIndex = 0;
		};

		//--------- variables ----------------------------------------------------

		// タイムスタンプ最大数(2つで1パス)
		static constexpr uint32_t kMaxTimestamps = 512;

		ComPtr<ID3D12QueryHeap> queryHeap_;
		ComPtr<ID3D12Resource> readbackBuffer_;
		uint64_t frequency_ = 0;
		bool initialized_ = false;

		// 記録中フラグ(BeginFrame～Resolve)
		bool active_ = false;
		uint32_t nextTimestamp_ = 0;
		std::vector<PassRecord> passes_;

		// 計測中のパス
		bool pendingPass_ = false;
		std::string pendingName_;
		uint32_t pendingBegin_ = 0;

		// 解決済み(前フレーム)の記録
		bool hasResolved_ = false;
		uint32_t resolvedCount_ = 0;
		std::vector<PassRecord> resolvedPasses_;

		//--------- functions ----------------------------------------------------

		// 遅延初期化(クエリヒープ/リードバックバッファ/周波数)
		bool EnsureInitialized(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
		// 解決済みデータを読み出してFrameProfilerへ反映する
		void CollectResolved();
	};
} // Engine
