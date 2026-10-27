#pragma once

//============================================================================
//	include
//============================================================================
#include <json.hpp>

namespace Engine {

	class GraphicsCore;

	//============================================================================
	//	RuntimeProjectSettings class
	//	ゲーム実行中の設定変更とユーザー保存を管理する
	//============================================================================
	class RuntimeProjectSettings {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit RuntimeProjectSettings(GraphicsCore& graphics, bool editor);
		// 初期値と保存済みのゲーム設定を取得する
		void Initialize();
		// 希望値を取得して次のframeへ適用を予約する
		nlohmann::json Read() const;
		nlohmann::json ReadEffective() const;
		bool Request(const nlohmann::json& settings);
		void ApplyPending();
		// Play終了時に編集前の設定へ戻す
		void BeginPlay();
		void EndPlay();
		// ユーザー設定を明示的に読み書きする
		bool SaveUserSettings() const;
		bool LoadUserSettings();
		void ResetToProjectDefaults();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		GraphicsCore& graphics_;
		bool editor_ = false;
		bool hasPending_ = false;
		bool playing_ = false;
		nlohmann::json defaults_;
		nlohmann::json original_;
		nlohmann::json pending_;

		//--------- functions ----------------------------------------------------

		// 現在の設定から保存可能な値だけを取り出す
		nlohmann::json Capture() const;
	};
}
