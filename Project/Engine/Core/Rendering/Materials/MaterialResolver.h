#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	MaterialResolver enum class
	//============================================================================
	// マテリアルのデフォルトスロットの種類
	enum class DefaultMaterialSlot : uint8_t {

		Sprite,
		Text,
		Mesh,
		MeshOutline,
		FullscreenCopy,
		Line,
		Primitive,
		Primitive2D,
	};

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

		// 指定IDを優先し、空なら登録済みの既定Materialを返す
		AssetID ResolveORDefault(const AssetDatabase& database, AssetID requested, DefaultMaterialSlot slot) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// デフォルトマテリアルのGUIDを取得
		static AssetID GetDefaultAssetID(DefaultMaterialSlot slot);
	};
} // Engine

