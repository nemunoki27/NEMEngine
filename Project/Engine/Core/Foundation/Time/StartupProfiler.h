#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <chrono>
#include <cstdint>

namespace Engine {

	//============================================================================
	//	StartupProfiler class
	//	Editorの初回表示までに必要な描画準備のCPU時間を集計する
	//============================================================================
	class StartupProfiler {
	public:
		enum class Stage : uint32_t {
			DXC, GraphicsPSO, ComputePSO, RootSignature,
			DXRLibrary, DXRRootSignature, DXRStateObject, DXRShaderTable, Count
		};

		static void Begin();
		static bool IsActive();
		static void SetFrame(bool ui, bool gameVisible, bool sceneVisible, bool gameReady, bool sceneReady);
		static void OnPresented();

		template<class Function>
		static auto Measure(Stage stage, Function&& function) {

			if (!IsActive()) return function();
			const auto begin = std::chrono::steady_clock::now();
			auto result = function();
			AddSample(stage, std::chrono::steady_clock::now() - begin);
			return result;
		}
	private:
		static void AddSample(Stage stage, std::chrono::steady_clock::duration elapsed);
	};
}
