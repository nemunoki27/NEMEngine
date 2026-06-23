#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <string>
#include <vector>
#include <utility>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	PrefabJsonDiff structures
	//============================================================================
	// コンポーネントマップ同士を比較した分類結果
	struct ComponentMapDiff {

		// 型名から始まるリーフ経路とインスタンス側の値の組
		std::vector<std::pair<std::string, nlohmann::json>> modifications;
		// インスタンスにのみ存在するコンポーネントの型名と値の組
		std::vector<std::pair<std::string, nlohmann::json>> addedComponents;
		// ベースにのみ存在するコンポーネントの型名
		std::vector<std::string> removedComponents;
	};

	//============================================================================
	//	PrefabJsonDiff namespace
	//	プレファブ差分のためのJSONリーフ経路操作
	//============================================================================
	namespace PrefabJsonDiff {

		// 経路のリーフ位置の値を取得する、無ければnullptr
		const nlohmann::json* GetAtPath(const nlohmann::json& root, const std::string& path);

		// 経路のリーフ位置へ値を設定する、途中のオブジェクトは必要なら作る
		void SetAtPath(nlohmann::json& root, const std::string& path, const nlohmann::json& value);

		// 2値のリーフ差分を集めてoutへ積む、配列とスカラーは丸ごと1リーフとして比較する
		void CollectLeafDifferences(const std::string& prefix, const nlohmann::json& base,
			const nlohmann::json& instance, std::vector<std::pair<std::string, nlohmann::json>>& out);

		// コンポーネントマップ同士を比較し型単位で差分を分類する、excludeTypesは比較から除外する
		ComponentMapDiff DiffComponentMaps(const nlohmann::json& base, const nlohmann::json& instance,
			const std::vector<std::string>& excludeTypes);
	}
} // Engine
