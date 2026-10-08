#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <string>

namespace Engine {

	class AssetDatabase;

	// 新規Graphの作成設定
	struct ShaderGraphCreationRequest {

		std::string path = "GameAssets/Materials/NewShader.shadergraph.json";
		ShaderGraphDomain domain = ShaderGraphDomain::Surface;
		ShaderGraphTarget target = ShaderGraphTarget::Mesh;
	};

	namespace ShaderGraphAssetAuthoring {

		// 新規Graphを保存してAssetへ登録する
		AssetID Create(AssetDatabase& database, const ShaderGraphCreationRequest& request, std::string& status);
		// 別Assetの設定と参照を検証して取り込む
		bool Import(AssetDatabase& database, const ShaderGraphAsset& destination, AssetID destinationID, AssetID source,
			ShaderGraphAsset& output, std::string& status);
	}
} // Engine
