#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>

// c++
#include <array>
#include <filesystem>

namespace Engine {

	//============================================================================
	//	MaterialResolver enum class
	//============================================================================

	// マテリアルのデフォルトスロットの種類
	enum class DefaultMaterialSlot :
		uint8_t {

		Sprite,
		Text,
		Mesh,
		FullscreenCopy,
	};
	static constexpr const uint32_t kDefaultMaterialCount = static_cast<uint32_t>(DefaultMaterialSlot::FullscreenCopy) + 1;

	//============================================================================
	//	MaterialResolver class
	//	使用するマテリアルを要求に応じて解決するクラス
	//============================================================================
	class MaterialResolver {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MaterialResolver() = default;
		~MaterialResolver() = default;

		// マテリアルIDを返す、存在しなければデフォルトマテリアルを返す
		AssetID ResolveORDefault(AssetDatabase& database, AssetID requested, DefaultMaterialSlot slot) const;

		// データクリア
		void Clear();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 初期化済みか
		mutable bool initialized_ = false;
		// デフォルトマテリアルのID配列
		mutable std::array<AssetID, kDefaultMaterialCount> defaultMaterials_{};

		//--------- functions ----------------------------------------------------

		// デフォルトマテリアルのIDを確保する
		void EnsureDefaults(AssetDatabase& database) const;
		// デフォルトマテリアルのパスを取得
		static const char* GetDefaultAssetPath(DefaultMaterialSlot slot);
	};
} // Engine