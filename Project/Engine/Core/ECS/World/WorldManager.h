#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>

namespace Engine {

	//============================================================================
	//	WorldManager class
	//	ワールドを所持、管理するクラス
	//============================================================================
	class WorldManager {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		WorldManager() = default;
		~WorldManager() = default;

		// プレイワールドの作成
		void CreatePlayWorld();
		// プレイワールドの破棄
		void DestroyPlayWorld();

		//--------- accessor -----------------------------------------------------

		// ゲームプレイ中か
		bool IsPlaying() const { return playWorld_ != nullptr; }

		// 現在アクティブな方のワールドを返す
		ECSWorld& GetActiveWorld() { return playWorld_ ? *playWorld_ : editWorld_; }
		const ECSWorld& GetActiveWorld() const { return playWorld_ ? *playWorld_ : editWorld_; }
		// エディタワールドの取得
		ECSWorld& GetEditWorld() { return editWorld_; }
		const ECSWorld& GetEditWorld() const { return editWorld_; }
		// プレイワールドの取得
		ECSWorld* GetPlayWorld() { return playWorld_.get(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// エディタ用ワールド、シーンの編集用
		ECSWorld editWorld_;
		// プレイ用ワールド、ゲーム実行中に更新される
		std::unique_ptr<ECSWorld> playWorld_;
	};
} // Engine