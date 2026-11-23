#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <memory>

namespace Engine {

	class AssetDatabase;
	struct MaterialAsset;
	struct EditorPanelContext;
	struct ValueEditResult;

	//============================================================================
	//	GraphMaterialAssetFields class
	//	グラフの定義に従ってマテリアルの共有値を編集する
	//============================================================================
	class GraphMaterialAssetFields {
	public:
		// グラフが更新されたとき表示用の定義を読み直す
		bool Refresh(const AssetDatabase& database, AssetID graphID);
		bool IsLoaded() const { return loaded_; }
		// グラフの参照と公開値を表示する
		ValueEditResult Draw(const EditorPanelContext& context, MaterialAsset& material, bool readOnly = false);

	private:
		std::weak_ptr<const uint8_t> databaseLifetime_;
		AssetID graphID_{};
		uint64_t structureRevision_ = 0;
		uint64_t graphRevision_ = 0;
		MaterialParameterSet defaults_;
		std::vector<MaterialParameterID> exposed_;
		std::string graphName_;
		std::string diagnostic_;
		bool loaded_ = false;
	};
}
