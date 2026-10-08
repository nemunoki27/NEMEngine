#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstddef>

namespace Engine {

	//============================================================================
	//	SceneEntityKey struct
	//	Sceneの実体と文書内IDの組み合わせでEntityを識別する
	//============================================================================
	struct SceneEntityKey {

		//--------- variables ----------------------------------------------------

		UUID sceneInstanceID{}; // Sceneの実体
		UUID localFileID{};		// 文書内の識別子

		//--------- functions ----------------------------------------------------

		// Sceneと文書内IDの一致を判定する
		bool operator==(const SceneEntityKey& other) const noexcept = default;
	};

	//============================================================================
	//	SceneEntityKeyHash struct
	//	Sceneと文書内IDの検索用ハッシュを計算する
	//============================================================================
	struct SceneEntityKeyHash {

		//--------- functions ----------------------------------------------------

		// 両方の識別子から検索用ハッシュを生成する
		size_t operator()(const SceneEntityKey& key) const noexcept;
	};
}
