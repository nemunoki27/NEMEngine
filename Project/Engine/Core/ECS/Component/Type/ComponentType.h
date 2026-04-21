#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <string>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	ComponentType struct
	//	コンポーネントの種類、情報を所持する
	//============================================================================
	struct ComponentTypeInfo {

		// 名前
		std::string name;
		// 一意なID
		uint32_t id = 0;

		// データサイズ、アライメント
		size_t size = 0;
		size_t align = 0;

		// コンストラクタ
		void (*constructDefault)(void* ptr) = nullptr;
		// デストラクタ
		void (*destroy)(void* ptr) = nullptr;
		// コピーコンストラクタ
		void (*moveConstruct)(void* dst, void* src) = nullptr;

		// json変換関数
		void (*from_json)(void* obj, const nlohmann::json& in) = nullptr;
		void (*to_json)(const void* obj, nlohmann::json& out) = nullptr;
	};
} // Engine
