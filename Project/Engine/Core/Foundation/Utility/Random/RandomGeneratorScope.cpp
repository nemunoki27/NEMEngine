#include "RandomGeneratorScope.h"

namespace {

	thread_local std::mt19937* currentSource = nullptr;
}

Engine::RandomGeneratorScope::RandomGeneratorScope(std::mt19937& source) : previous_(currentSource) {

	// 入れ子の呼出しでも外側の乱数列を保持する
	currentSource = &source;
}

Engine::RandomGeneratorScope::~RandomGeneratorScope() {

	currentSource = previous_;
}

std::mt19937& Engine::RandomGeneratorScope::GetSource() {

	if (currentSource) return *currentSource;
	// Instanceを指定しない処理はThreadごとの乱数列を使う
	thread_local std::mt19937 source(std::random_device{}());
	return source;
}
