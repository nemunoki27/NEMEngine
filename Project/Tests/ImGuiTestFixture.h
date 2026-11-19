#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>

// c++
#include <filesystem>
// imgui
#include <imgui.h>

namespace NEMTests {

	//============================================================================
	//	ImGuiTestFixture class
	//	OSの入力を使わず実際のUIを描画して画像を保存する
	//============================================================================
	class ImGuiTestFixture {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ImGuiTestFixture();
		~ImGuiTestFixture();
		// 次のUIフレームを開始する
		void Begin();
		// 描画結果を確定して必要ならPNGへ保存する
		bool End(const std::filesystem::path& imagePath = {});
		// GPUのエラー診断を確認する
		bool CheckMessages();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleGPUFixture gpu_;
		Engine::RTVDescriptor targets_;
		Engine::RenderTexture2D output_;
		ImGuiContext* previous_ = nullptr;
		ImGuiContext* context_ = nullptr;

		//--------- functions ----------------------------------------------------

		// 描画先を読み戻してPNGへ保存する
		bool SaveImage(const std::filesystem::path& path);
	};
}
