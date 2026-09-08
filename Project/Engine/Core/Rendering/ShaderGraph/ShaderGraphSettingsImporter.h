#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <functional>

namespace Engine {

	using ShaderGraphImportResolver =
		std::function<bool(AssetID, AssetType, nlohmann::json&)>;

	//============================================================================
	//	ShaderGraphSettingsImporter class
	//	アセットの設定を検証して編集用グラフへ変換する
	//============================================================================
	class ShaderGraphSettingsImporter {
	public:
		ShaderGraphSettingsImporter() = delete;

		// 成功時だけ出力を更新し、コピー元や保存先には書き込まない
		static bool Import(const ShaderGraphAsset& destination, AssetID source,
			AssetType sourceType, const ShaderGraphImportResolver& resolver,
			ShaderGraphAsset& output, std::string& error);
	};
}
