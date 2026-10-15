#pragma once

//============================================================================
//	include
//============================================================================
#include "BakedLightingInput.h"

// c++
#include <memory>

namespace Engine {

	class GraphicsCore;
	class AssetDatabase;
	struct BakedLightingBakeState;

	//============================================================================
	//	BakedLightingBake class
	//	CPU準備と分割GPUベイクの寿命を管理する
	//============================================================================
	class BakedLightingBake {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BakedLightingBake();
		~BakedLightingBake();

		// 索引を複製して保存時点の入力を保持する
		void Start(BakedLightingInput input, const BakedLightingSettings& settings, const AssetDatabase& database);
		// 記録中の描画Frameへ小さい計算単位を追加する
		void Tick(GraphicsCore& graphics);
		// 未公開の計算を中止する
		void Cancel();
		// WorkerとGPU資源の所有を解除する
		void Release();

		//--------- accessor -----------------------------------------------------

		bool IsRunning() const;
		float GetProgress() const;
		std::string GetStatus() const;
		std::shared_ptr<const BakedLightingData> GetResult() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unique_ptr<BakedLightingBakeState> state_;
	};
}
