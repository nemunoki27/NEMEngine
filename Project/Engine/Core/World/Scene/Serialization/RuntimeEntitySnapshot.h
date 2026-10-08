#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/EntityTreeSnapshot.h>

namespace Engine {

	//============================================================================
	//	RuntimeEntitySnapshot class
	//	実行中のComponentとScript保存値を複製用に取得する
	//============================================================================
	class RuntimeEntitySnapshot {
	public:
		// callback中にWorldが変更された場合は結果を公開しない
		static bool Capture(ECSWorld& world, const Entity& root, EntityTreeSnapshot& result);
	};
}
