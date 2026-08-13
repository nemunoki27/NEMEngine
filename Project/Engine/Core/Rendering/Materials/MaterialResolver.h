#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>

// c++
#include <array>

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
		MeshOutline,
		FullscreenCopy,
		Line,
		Primitive,
		Primitive2D,
	};
	static constexpr const uint32_t kDefaultMaterialCount = static_cast<uint32_t>(DefaultMaterialSlot::Primitive2D) + 1;

	//============================================================================
	//	MaterialResolver class
	//	使用するマテリアルを要求に応じて解決するクラス
	//============================================================================
	class MaterialResolver {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MaterialResolver() = default;
		~MaterialResolver() = default;

		// マテリアルIDを返す、存在しなければデフォルトマテリアルを返す
		AssetID ResolveORDefault(AssetDatabase& database, AssetID requested, DefaultMaterialSlot slot) const;

		// データクリア
		void Clear();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初期化済みか
		mutable bool initialized_ = false;
		// デフォルトマテリアルのID配列
		mutable std::array<AssetID, kDefaultMaterialCount> defaultMaterials_{};

		//--------- functions ----------------------------------------------------

		// デフォルトマテリアルのIDを確保する
		void EnsureDefaults(AssetDatabase& database) const;
		// デフォルトマテリアルのGUIDを取得
		static AssetID GetDefaultAssetID(DefaultMaterialSlot slot);
	};
} // Engine

