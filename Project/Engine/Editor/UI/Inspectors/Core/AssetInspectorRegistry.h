#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>

// c++
#include <cstddef>
#include <memory>
#include <vector>

namespace Engine {

	//============================================================================
	//	AssetInspectorRegistry class
	//	アセット種別ごとの描画処理を登録して取得
	//============================================================================
	class AssetInspectorRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetInspectorRegistry() = default;
		~AssetInspectorRegistry();

		// 種別の描画処理を登録
		bool Register(std::unique_ptr<IAssetInspectorDrawer> drawer);
		//--------- accessor -----------------------------------------------------

		// 種別から編集用の描画処理を取得
		IAssetInspectorDrawer* Find(AssetType type);
		// 種別から読み取り用の描画処理を取得
		const IAssetInspectorDrawer* Find(AssetType type) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 種別ごとに所有する描画処理
		std::vector<std::unique_ptr<IAssetInspectorDrawer>> drawers_{};

		//--------- functions ----------------------------------------------------

		// 指定種別が登録済みか
		bool HasDrawer(AssetType type) const;
		// 種別に対応する位置を検索
		std::size_t FindIndex(AssetType type) const;
	};
} // Engine
