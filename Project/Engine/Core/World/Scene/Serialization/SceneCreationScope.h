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

		ECSWorld& world_;								   // 生成と階層復元を行うWorld
		std::shared_ptr<const ECSWorldLifetime> lifetime_; // 生成元のWorldの寿命
		ECSCreationScope creation_;						   // 未確定の生成物
		bool committed_ = false;						   // 生成結果の確定
	};
}
