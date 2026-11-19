#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include "TestFixtures.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodePreviewCompiler.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>

// c++
#include <array>
#include <vector>

namespace Engine { struct ShaderGraphTimeConstantsGPU; }

namespace NEMTests {

	//============================================================================
	//	ShaderGraphNodePreviewGPUFixture class
	//	実際のノード評価Shaderを描画して値を読み戻す
	//============================================================================
	class ShaderGraphNodePreviewGPUFixture {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphNodePreviewGPUFixture();
		~ShaderGraphNodePreviewGPUFixture();

		// 本番と同じ定数配置とSamplerを使って評価する
		std::vector<Engine::Vector4> Render(const Engine::ShaderGraphNodePreviewCompileOutput& output,
			float time = 1.25f, float deltaTime = 0.02f, float smoothDeltaTime = 0.03f,
			const Engine::ShaderGraphTimeConstantsGPU* frameOverride = nullptr, uint32_t imageSize = GetSize(),
			const Engine::Vector4& clearColor = Engine::Vector4(-999, -999, -999, -999));
		// 検証用Textureの参照先を取得する
		static Engine::AssetID GetTextureID() { return {71, 12}; }
		// 読戻し画像の一辺を取得する
		static constexpr uint32_t GetSize() { return 16; }
		// Deviceからエラーが報告されていないか確認する
		bool CheckMessages();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleGPUFixture gpu_;
		TestDirectory directory_;
		Engine::RTVDescriptor targets_;
		Engine::RenderTexture2D color_;
		Engine::RenderTexture2D texture_;
		std::array<Engine::RenderTexture2D, 7> sceneTextures_;
		uint32_t shaderIndex_ = 0;

		//--------- functions ----------------------------------------------------

		// 入力のScene Textureと四隅の色を用意する
		void CreateInputs();
		// 描画先から全画素を読み戻す
		std::vector<Engine::Vector4> ReadOutput();
	};
}
