#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessAnchor.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	PostProcessStack structures
	//============================================================================
	// ポストプロセスの1パス分の設定データ
	struct PostProcessStackPassSettings {

		// パスの識別IDで並び替え後も参照を保つ
		UUID id{};
		// 表示名
		std::string name;
		// 有効フラグ
		bool enabled = true;
		// 実行するMaterialアセットのGUID
		AssetID materialGuid{};
		// 実行するパス種別
		MaterialPassKind passKind = MaterialPassKind::PostProcess;
		// このパスを差し込む固定パス上の位置
		PostProcessAnchor anchor = PostProcessAnchor::AfterMaskedUI;
		// CBufferパラメータのScene毎overrideマップ
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides;
		// TextureのScene毎overrideマップ
		std::unordered_map<std::string, AssetID> textureGuids;
		// SRVバインド名から中間RT名(GBuffer/深度など)への割り当て、.pngより優先される
		std::unordered_map<std::string, std::string> renderTargetInputs;
	};

	// シーンごとのPostProcessStackの設定データ
	struct PostProcessStackSettings {

		int version = 1;
		std::vector<PostProcessStackPassSettings> passes;
	};
} // Engine
