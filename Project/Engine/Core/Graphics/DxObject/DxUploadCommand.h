#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// directX
#include <d3d12.h>
// c++
#include <cstdint>

//============================================================================
//	DxUploadCommand class
//	アップロード専用のコマンドキュー/リストでリソース転送を実行・同期する。
//============================================================================
namespace Engine {

class DxUploadCommand {
public:
	//========================================================================
	//	public Methods
	//========================================================================

	DxUploadCommand() = default;
	~DxUploadCommand() = default;

	// アップロード用のキュー/アロケータ/リスト/フェンスを作成する
	void Create(ID3D12Device* device);

	// 転送用コマンドをキューへ提出し完了させる
	void ExecuteCommands(ID3D12CommandQueue* waitQueue = nullptr);

	//--------- accessor -----------------------------------------------------

	// コマンドリストを取得する
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }
private:
	//========================================================================
	//	private Methods
	//========================================================================

	//--------- variables ----------------------------------------------------

	ComPtr<ID3D12GraphicsCommandList> commandList_;
	ComPtr<ID3D12CommandAllocator> commandAllocator_;

	ComPtr<ID3D12CommandQueue> commandQueue_;

	ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_;
	HANDLE fenceEvent_;

	//--------- functions ----------------------------------------------------

	// 内部ヘルパ: コマンドアロケータ/リストをリセットする
	void ResetCommand();
};

}; // Engine
