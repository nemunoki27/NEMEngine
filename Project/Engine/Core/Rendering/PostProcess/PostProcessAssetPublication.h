#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>
#include <span>
#include <vector>

namespace Engine {

	class AssetDatabase;

	// Compute描画Assetの生成元と保存先
	struct PostProcessAssetSource {

		std::string name;		  // 生成名
		std::string sourceShader; // 元HLSLの論理パス
		std::string shaderFile;	  // Shaderのステージ設定
		std::string shaderPath;	  // Shader文書の保存先
		std::string pipelinePath; // Pipeline文書の保存先
		std::string materialPath; // Material文書の保存先
		bool builtin = false;	  // 標準パラメータの適用
	};

	//============================================================================
	//	PostProcessAssetPublication namespace
	//============================================================================
	namespace PostProcessAssetPublication {

		// 複数効果の文書と索引をまとめて確定する
		bool PublishBatch(AssetDatabase& database, std::span<const PostProcessAssetSource> sources,
			std::vector<AssetID>& output, std::string& diagnostic);

		// 文書とmetaをまとめて保存し、成功後に識別子を公開する
		AssetID Publish(AssetDatabase& database, const PostProcessAssetSource& source, std::string& diagnostic);
	}
}
