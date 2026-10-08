#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Utility/Random/RandomGeneratorScope.h>

// c++
#include <random>
#include <type_traits>
#include <utility>

//============================================================================
//	RandomGenerator class
//	範囲を並べ替えて乱数を生成する
//============================================================================
namespace Engine {

	class RandomGenerator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 指定範囲から乱数を生成する
		template <typename T>
		static T Generate(T minimum, T maximum);

		// 各座標の範囲から生成する
		static Engine::Vector3 Generate(const Engine::Vector3& min, const Engine::Vector3& max);
		// 各色成分の範囲から生成する
		static Color4 Generate(const Engine::Color4& min, const Engine::Color4& max);
	};

	//============================================================================
	//	RandomGenerator classTemplateMethods
	//============================================================================

	template <typename T>
	inline T RandomGenerator::Generate(T minimum, T maximum) {

		static_assert(std::is_arithmetic_v<T>, "乱数の範囲には数値型を指定してください");
		// 範囲が逆なら入れ替える
		if (minimum > maximum) {
			std::swap(minimum, maximum);
		}
		// 処理範囲の乱数列を取得
		std::mt19937& source = RandomGeneratorScope::GetSource();
		// 整数と実数の分布を選択
		if constexpr (std::is_integral_v<T>) {
			std::uniform_int_distribution<T> distribution(minimum, maximum);
			return distribution(source);
		} else if constexpr (std::is_floating_point_v<T>) {
			std::uniform_real_distribution<T> distribution(minimum, maximum);
			return distribution(source);
		}
	}

}
