#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <filesystem>

namespace Engine {

	// 描画種別ごとに保存する既定MaterialのGUID
	struct DefaultMaterialConfiguration {

		AssetID mesh{}; // Meshの既定ID
		AssetID sprite{}; // Spriteの既定ID
		AssetID text{}; // Textの既定ID
		AssetID line{}; // Lineの既定ID
		AssetID primitive{}; // 3D形状の既定ID
		AssetID primitive2D{}; // 2D形状の既定ID
		AssetID raytracingReflection{}; // 反射の既定ID
	};
}

namespace Engine::DefaultMaterialConfigurationIO {

	// ファイルが無ければ未設定として読み込む
	bool Read(const std::filesystem::path& path, DefaultMaterialConfiguration& output);
	// GUIDを既存の設定形式で保存する
	bool Write(const std::filesystem::path& path, const DefaultMaterialConfiguration& configuration);
}
