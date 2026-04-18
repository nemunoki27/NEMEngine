#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/Graphics/Asset/ShaderAsset.h>
#include <Engine/Core/Graphics/Asset/RenderPipelineAsset.h>
#include <Engine/Core/Graphics/Asset/MaterialAsset.h>
#include <Engine/Core/Graphics/Asset/MSDFFontAsset.h>

// c++
#include <unordered_map>
#include <filesystem>
#include <cctype>

namespace Engine {

	//============================================================================
	//	RenderAssetLibrary class
	//	描画系アセットをロード、キャッシュするクラス
	//============================================================================
	class RenderAssetLibrary {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderAssetLibrary() = default;
		~RenderAssetLibrary() = default;

		// 初期化
		void Init(AssetDatabase* database);

		// データクリア
		void Clear();

		// アセットIDからデータをロード
		const ShaderAsset* LoadShader(AssetID assetID);
		const RenderPipelineAsset* LoadPipeline(AssetID assetID);
		const MaterialAsset* LoadMaterial(AssetID assetID);
		const MSDFFontAsset* LoadFont(AssetID assetID);

		//--------- accessor -----------------------------------------------------

		AssetDatabase* GetDatabase() const { return database_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetDatabase* database_ = nullptr;

		// アセットIDからデータへのマップ
		std::unordered_map<AssetID, ShaderAsset> shaderCache_;
		std::unordered_map<AssetID, RenderPipelineAsset> pipelineCache_;
		std::unordered_map<AssetID, MaterialAsset> materialCache_;
		std::unordered_map<AssetID, MSDFFontAsset> fontCache_;
	};
} // Engine