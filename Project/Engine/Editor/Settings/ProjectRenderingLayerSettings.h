#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <cstdint>
#include <string>

namespace Engine::ProjectRenderingLayerSettings {

	inline constexpr uint32_t kLayerCount = 24u;

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

} // Engine::ProjectRenderingLayerSettings
