#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAsset.h"

// c++
#include <string_view>
#include <unordered_set>

namespace Engine {

	// 生成した値の型とHLSL式
	struct ShaderGraphExpression {

		ShaderGraphValueType type = ShaderGraphValueType::Invalid;
		std::string code;
	};

	namespace ShaderGraphSourceUtility {

		bool SupportsHexTiling(const ShaderGraphAsset& graph);
		// 数値型の成分数を取得する
		uint32_t ComponentCount(ShaderGraphValueType type);
		// 数値型か判定する
		bool IsNumeric(ShaderGraphValueType type);
		// HLSLの型名を取得する
		std::string HLSLType(ShaderGraphValueType type);
		// 公開値の識別子を作る
		std::string MakeIdentifier(std::string_view name, Engine::UUID id);
		// Nodeの一時変数名を作る
		std::string MakeNodeVariable(std::string_view prefix, Engine::UUID id);
		// グラフ内で使用中の識別子を収集する
		std::unordered_set<uint64_t> CollectIdentifiers(const ShaderGraphAsset& graph);
		// 用途ごとの衝突しない識別子を作る
		Engine::UUID MakeUniqueID(std::unordered_set<uint64_t>& identifiers,
			uint64_t seed, Engine::UUID owner, uint32_t channel);
		// 保存値をHLSLリテラルへ変換する
		std::string MakeLiteral(const MaterialParameterValue& value, ShaderGraphValueType type);
		// 接続値を入力の型へ変換する
		ShaderGraphExpression ConvertExpression(ShaderGraphExpression expression, ShaderGraphValueType target);
	} // ShaderGraphSourceUtility
} // Engine
