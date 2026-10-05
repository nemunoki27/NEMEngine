#pragma once

//============================================================================
//	include
//============================================================================
#include <random>

namespace Engine {

	//============================================================================
	//	RandomGeneratorScope class
	//	乱数の生成先を処理範囲内だけ所有Instanceへ切り替える
	//============================================================================
	class RandomGeneratorScope {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit RandomGeneratorScope(std::mt19937& source);
		~RandomGeneratorScope();
		RandomGeneratorScope(const RandomGeneratorScope&) = delete;
		RandomGeneratorScope& operator=(const RandomGeneratorScope&) = delete;
		static std::mt19937& GetSource();
	private:
		//--------- variables ----------------------------------------------------

		std::mt19937* previous_ = nullptr;
	};
}
