#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstddef>
#include <cstdint>

namespace Engine {

	//============================================================================
	//	ECSConfig
	//	ECSの設定、定数
	//============================================================================
	// 使用可能なコンポーネント種類の最大数
	static constexpr uint32_t kMaxComponentTypes = 256;

	// アーキタイプ内でまとめて保持するチャンクのバイト数
	static constexpr size_t kChunkBytes = 16 * 1024;
	// チャンク先頭のアライメント
	static constexpr size_t kChunkAlignment = 64;
	// 小さいコンポーネントだけのアーキタイプでエンティティ数が増えすぎるのを防ぐ上限
	static constexpr uint32_t kMaxChunkEntities = 1024;
} // Engine
