#include "RandomGeneratorScope.h"

namespace {

	// 現在の処理範囲で使う乱数列
	thread_local std::mt19937* currentSource = nullptr;
}

//============================================================================
//	RandomGeneratorScope classMethods
//============================================================================
Engine::RandomGeneratorScope::RandomGeneratorScope(std::mt19937& source) : previous_(currentSource) {

	// 入れ子の呼出しでも外側の乱数列を保持する
	currentSource = &source;
}

Engine::RandomGeneratorScope::~RandomGeneratorScope() {

	// 外側の乱数列へ戻す
	currentSource = previous_;
}

std::mt19937& Engine::RandomGeneratorScope::GetSource() {

	if (currentSource) {
		return *currentSource;
	}
	// 指定がなければスレッドごとの乱数列を使用
	thread_local std::mt19937 source(std::random_device{}());
	return source;
}
