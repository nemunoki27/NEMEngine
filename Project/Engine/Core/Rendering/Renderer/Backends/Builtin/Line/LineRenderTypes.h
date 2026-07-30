#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>

// c++
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	LineRenderTypes structures
	//============================================================================
	// ライン描画データ
	// 点列はコンポーネントor即時バッファを指す、同フレーム内のみ有効
	struct LineRenderPayload {

		const LinePoint* points = nullptr;
		uint32_t pointCount = 0;

		// trueなら隣接点を連結したポリライン、falseなら2点ずつ独立した線分リスト
		bool connected = true;
		// 始点と終点をつないで閉じるか、connectedのときのみ有効
		bool loop = false;
		// 2D描画か、trueなら正射影で点のxyのみ使う
		bool is2D = false;
		// 点をワールド絶対座標として扱うか、falseならitem.worldMatrixで変換する
		bool useWorldSpace = true;

		// エンティティごとのマテリアルパラメータ上書き、描画時に既定値へ重ねる
		const MaterialParameterSet* materialInstance = nullptr;
	};
} // Engine
