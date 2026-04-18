#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <list>
#include <numeric>
#include <chrono>

namespace Engine {

	//============================================================================
	//	FrameTimer class
	//	フレームごとの時間を計測するクラス
	//============================================================================
	class FrameTimer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		FrameTimer() = default;
		~FrameTimer() = default;

		// フレームタイマーの初期化
		void Init();

		// フレーム時間の更新
		void Update();

		// 時間計測
		// 更新
		void BeginUpdateCount();
		void EndUpdateCount();
		// 描画
		void BeginDrawCount();
		void EndDrawCount();

		//--------- accessor -----------------------------------------------------

		// ゲームにおける1フレームの時間を取得
		float GetDeltaTime() const { return deltaTime_; }
		// 起動してからの合計時間を取得
		float GetTotalTime() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 計測情報
		struct Measurement {

			std::chrono::time_point<std::chrono::high_resolution_clock> start; // 開始時間
			std::chrono::time_point<std::chrono::high_resolution_clock> end;   // 終了時間
			std::chrono::duration<float, std::milli> resultSeconds; // 実行時間

			// 各フレームの処理時間
			std::list<float> times;
		};

		//--------- variables ----------------------------------------------------

		// ゲームにおける1フレームの時間
		float deltaTime_ = 0.0f;

		// 起動開始からの時間
		std::chrono::steady_clock::time_point startTime_;
		// 前フレームの時間
		std::chrono::steady_clock::time_point preFrameTime_;

		// 更新処理の計測情報
		Measurement updateMeasure_{};
		// 描画処理の計測情報
		Measurement drawMeasure_{};

		//--------- functions ----------------------------------------------------

		// 計測平均時間を取得
		float GetAverageTime(const Measurement& measure) const;

		// 計測時間処理
		void BeginMeasure(Measurement& measure);
		void EndMeasure(Measurement& measure);
	};
} // Engine