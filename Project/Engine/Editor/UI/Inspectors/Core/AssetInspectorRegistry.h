#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	//============================================================================
	//	AssetInspectorRegistry class
	//	Asset種別ごとのInspector Drawerを登録し種別から引く
	//============================================================================
	class AssetInspectorRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetInspectorRegistry() = default;
		~AssetInspectorRegistry();

		// Drawerを登録する
		bool Register(std::unique_ptr<IAssetInspectorDrawer> drawer);
		// 種別からDrawerを取得する、無ければnullptr
		IAssetInspectorDrawer* Find(AssetType type) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 登録されたDrawer、種別ごとに一意
		std::vector<std::unique_ptr<IAssetInspectorDrawer>> drawers_{};

		//--------- functions ----------------------------------------------------

		// 指定AssetTypeが登録済みか
		bool HasDrawer(AssetType type) const;
	};
} // Engine
