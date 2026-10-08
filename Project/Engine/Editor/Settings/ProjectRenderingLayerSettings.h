#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <cstdint>
#include <string>

namespace Engine {

	//============================================================================
	//	ProjectRenderingLayerSettings class
	//	Projectの設定文書と編集条件を保持するクラス
	//============================================================================
	class ProjectRenderingLayerSettings {
	public:
		static constexpr uint32_t kLayerCount = 24u;
		//========================================================================
		//	public Methods
		//========================================================================

		// ProjectSettingsのRendering Layer名を取得する
		const std::array<std::string, kLayerCount>& GetNames();

		// 現在登録されているRendering Layerのbitを取得する
		uint32_t GetDefinedMask();

		// 新規Layer名として追加できるか
		bool IsValidNewLayer(const std::string& name);

		// 空いているLayer番号へ追加する
		bool AddLayer(const std::string& name);

		// 指定Layerの名前を変更する
		bool SetName(uint32_t index, const std::string& name);

		// 指定Layerを未使用へ戻す、Defaultは削除できない
		bool RemoveLayer(uint32_t index);

		// ファイルから読み直す
		void Reload();

		// 現在のLayer名をファイルへ保存する
		bool Save();

		//--------- accessor -----------------------------------------------------

		bool IsDirty() const { return dirty_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::array<std::string, kLayerCount> names_{};
		bool loaded_ = false;
		bool dirty_ = false;

		//--------- functions ----------------------------------------------------

		// 予約Layerを初期化する
		void SetDefaults();
		// 登録済みの名前を照合する
		bool ContainsName(const std::string& name, uint32_t ignoredIndex = kLayerCount);
		// 文書を読み既定値を補う
		void LoadFromDisk();
		// 初回の文書読込を行う
		void EnsureLoaded();
	};
}
