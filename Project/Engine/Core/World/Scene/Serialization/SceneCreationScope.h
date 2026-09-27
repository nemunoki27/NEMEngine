#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSCreationScope.h>

namespace Engine {

	//============================================================================
	//	SceneCreationScope class
	//	SceneとPrefabの生成取消後に既存階層を復元する
	//============================================================================
	class SceneCreationScope {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit SceneCreationScope(ECSWorld& world);
		~SceneCreationScope();
		SceneCreationScope(const SceneCreationScope&) = delete;
		SceneCreationScope& operator=(const SceneCreationScope&) = delete;

		// 生成結果を確定する
		void Commit();
		// 生成したEntityへ削除差分を適用する
		void DestroyCreated(const Entity& entity);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ECSWorld& world_;
		ECSCreationScope creation_;
		bool committed_ = false;
	};
}
