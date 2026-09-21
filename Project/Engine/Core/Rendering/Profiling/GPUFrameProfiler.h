#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>
#include <cstdint>
#include <string>
#include <vector>
// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	GPUFrameProfiler class
	// D3D12タイムスタンプクエリで描画パスごとのGPU処理時間を計測するシングルトン
	// 各フレームContextの再利用時に、そのContextの解決結果だけを安全に読み戻す
	//============================================================================
	class GPUFrameProfiler {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		static GPUFrameProfiler& GetInstance();

		// フレーム開始で初回は遅延初期化し前フレームの計測結果をFrameProfilerへ反映してから記録をリセットする
		void BeginFrame(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
		// パス計測(BeginFrame～Resolveの間のみ有効)
		void BeginPass(ID3D12GraphicsCommandList* commandList, const std::string& name);
		void EndPass(ID3D12GraphicsCommandList* commandList);
		// 記録を締め切り、ResolveQueryDataをコマンドリストへ積む
		void Resolve(ID3D12GraphicsCommandList* commandList);

		// 終了処理
		void Finalize();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		GPUFrameProfiler() = default;

		//--------- structure ----------------------------------------------------

		// パスごとの計測範囲(タイムスタンプのインデックス)
		struct PassRecord {

			std::string name;
			uint32_t beginIndex = 0;
			uint32_t endIndex = 0;
		};
		struct FrameQueryState {

			ComPtr<ID3D12QueryHeap> queryHeap{};
			ComPtr<ID3D12Resource> readbackBuffer{};

			bool active = false;
			uint32_t nextTimestamp = 0;
			std::vector<PassRecord> passes{};

			bool pendingPass = false;
			std::string pendingName{};
			uint32_t pendingBegin = 0;

			bool hasResolved = false;
			uint32_t resolvedCount = 0;
			std::vector<PassRecord> resolvedPasses{};
		};

		//--------- variables ----------------------------------------------------

		// タイムスタンプ最大数(2つで1パス)
		static constexpr uint32_t kMaxTimestamps = 512;

		std::array<FrameQueryState, kGraphicsFrameContextCount> frameStates_{};
		uint64_t frequency_ = 0;
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 遅延初期化(クエリヒープ/リードバックバッファ/周波数)
		bool EnsureInitialized(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
		// 解決済みデータを読み出してFrameProfilerへ反映する
		void CollectResolved(FrameQueryState& state);
	};
} // Engine
