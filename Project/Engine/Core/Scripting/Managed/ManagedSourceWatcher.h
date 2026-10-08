#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Watch/AssetChangeWatcher.h>

namespace Engine {

	//============================================================================
	//	ManagedSourceWatcher class
	// C#ソースの変更通知を共通のディレクトリ監視から取得する
	//============================================================================
	class ManagedSourceWatcher {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ManagedSourceWatcher() = default;
		~ManagedSourceWatcher();

		ManagedSourceWatcher(const ManagedSourceWatcher&) = delete;
		ManagedSourceWatcher& operator=(const ManagedSourceWatcher&) = delete;

		// 監視を開始する、既に監視中なら一度停止してから張り直す
		bool Start(const std::filesystem::path& directory);
		// 監視を停止しスレッドを終了する、複数回呼んでも安全
		void Stop();

		//--------- accessor -----------------------------------------------------

		// 前回の確認以降に変更があったかを取り出してフラグをクリアする
		bool ConsumeChanged();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetChangeWatcher watcher_;
	};
}
