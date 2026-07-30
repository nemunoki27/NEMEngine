#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxDevice.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandQueue.h>
#include <Engine/Core/Rendering/DxObject/Core/FramePresenter.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureController.h>

// directX
#include <dxgidebug.h>
#include <dxgi1_6.h>

namespace Engine {

	//============================================================================
	//	GraphicsPlatform class
	//	Device/Command/ShaderCompilerとGPU機能状態を管理する
	//============================================================================
	class GraphicsPlatform {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		GraphicsPlatform() = default;
		~GraphicsPlatform() = default;

		// DxDevice/Command/Queue/Presenter/DxShaderCompilerの生成と初期化を行う
		void Init();

		// コマンドキュー終端などの後処理を実行し、各リソースを破棄する
		void Finalize(HWND hwnd);

		// フレーム終端でコマンドを提出しPresentまで行う
		void PresentFrame(IDXGISwapChain4* swapChain);
		// 指定フレームコンテキストの完了を待って記録を開始する
		void BeginFrame(uint32_t frameIndex);

		// 現在積んでいるコマンドを実行しGPU完了まで待機する
		void WaitForGPU();

		//--------- accessor -----------------------------------------------------

		ID3D12Device8* GetDevice() const { return dxDevice_->Get(); }
		IDXGIFactory7* GetDxgiFactory() const { return dxDevice_->GetDxgiFactory(); }

		DxCommand* GetDxCommand() const { return dxCommand_.get(); }
		DxCommandQueue* GetCommandQueue() const { return dxCommandQueue_.get(); }
		DxShaderCompiler* GetDxShaderCompiler() const { return dxShaderComplier_.get(); }

		GraphicsFeatureController& GetFeatureController() { return featureController_; }
		const GraphicsFeatureController& GetFeatureController() const { return featureController_; }

		bool ShouldUseMeshShader() const { return featureController_.ShouldUseMeshShader(); }
		bool ShouldUseInlineRayTracing() const { return featureController_.ShouldUseInlineRayTracing(); }
		bool ShouldUseDispatchRays() const { return featureController_.ShouldUseDispatchRays(); }
		bool ShouldBuildRaytracingScene() const { return featureController_.ShouldBuildRaytracingScene(); }
		bool ShouldUseRayTracing() const { return featureController_.ShouldBuildRaytracingScene(); }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::unique_ptr<DxDevice> dxDevice_;
		std::unique_ptr<DxCommandQueue> dxCommandQueue_;
		std::unique_ptr<DxCommand> dxCommand_;
		std::unique_ptr<FramePresenter> framePresenter_;
		std::unique_ptr<DxShaderCompiler> dxShaderComplier_;

		GraphicsFeatureController featureController_{};

		//--------- functions ----------------------------------------------------

		void InitDXDevice();
		void DetectFeatureSupport();
		D3D_SHADER_MODEL QueryHighestShaderModel() const;
	};
}; // Engine
