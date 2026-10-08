#pragma once

//============================================================================
//	include
//============================================================================
// json
#include <json.hpp>

namespace msdf_atlas {

	class FontGeometry;
	class TightAtlasPacker;
}

namespace Engine::MSDFAtlasDocument {

	// 配置済みの文字を従来の上原点の保存形式へ変換する
	nlohmann::json Build(const msdf_atlas::FontGeometry& font, const msdf_atlas::TightAtlasPacker& packer);
}
