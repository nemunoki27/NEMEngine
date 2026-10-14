#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <cstdint>
#include <span>
#include <vector>

// directX
#include <d3d12.h>

//============================================================================
//	DxUploadCommand class
// アップロード専用のコマンドキュー/リストでリソース転送を実行・同期する
//============================================================================
namespace Engine {

class DxUploadCommand {
public:
	//============================================================================
	//	public Methods
	//============================================================================

	DxUploadCommand() = default;
	~DxUploadCommand();
	DxUploadCommand(const DxUploadCommand&) = delete;
	DxUploadCommand& operator=(const DxUploadCommand&) = delete;

	// アップロード用のキュー/アロケータ/リスト/フェンスを作成する
	void Create(ID3D12Device* device, ID3D12CommandQueue* queue = nullptr);

	// 転送用コマンドをキューへ提出し完了させる
	void ExecuteCommands(std::span<const ComPtr<ID3D12Resource>> resources);
	// 記録するコマンドの転送元と転送先を保持する
	void RetainResources(std::span<const ComPtr<ID3D12Resource>> resources);
	// 記録済みの転送を提出する
	void SubmitCommands(std::span<const ComPtr<ID3D12Resource>> resources = {});
	// 完了済みの転送だけを確定して次の記録を準備する
	bool TryFinalize();

	// 提出済みの転送を完了させる
	void FlushAndWait();

	//--------- accessor -----------------------------------------------------

	// コマンドリストを取得する
	ID3D12GraphicsCommandList* GetCommandList() const;
private:
	//============================================================================
	//	private Methods
	//============================================================================

	//--------- variables ----------------------------------------------------

	ComPtr<ID3D12Device> device_;

	ComPtr<ID3D12GraphicsCommandList> commandList_;
	ComPtr<ID3D12CommandAllocator> commandAllocator_;

	ComPtr<ID3D12CommandQueue> commandQueue_;

	ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

	std::vector<ComPtr<ID3D12Resource>> retainedResources_;
	bool recording_ = false;
	bool submitted_ = false;
	bool signaled_ = false;

	//--------- functions ----------------------------------------------------

	// 内部ヘルパ:コマンドアロケータ/リストをリセットする
	void ResetCommand();
	// 提出済みの転送にFence値を対応付ける
	void SignalCommands();
};

}; // Engine

