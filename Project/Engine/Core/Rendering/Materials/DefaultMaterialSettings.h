#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	DefaultMaterialSettings class
	// Mesh/Sprite/Textのデフォルトマテリアルを保持し設定ファイルへ永続化するシングルトン
	// 空マテリアルの解決先をbuiltinから差し替える用途で、未設定スロットはbuiltinへフォールバックする
	//============================================================================
	class DefaultMaterialSettings {
	public:
		//========================================================================
		//	public Methods
		//========================================================================
		DefaultMaterialSettings() = default;
		~DefaultMaterialSettings() = default;

		// 設定ファイルパスを記憶して値を読み込む、ファイルが無ければ未設定のまま
		void Load(const std::string& configPath);
		// 現在の設定を記憶済みのパスへ保存する
		void Save() const;

		//--------- accessor -----------------------------------------------------

		// 設定値の取得、未設定なら空IDを返す
		AssetID GetMesh() const { return mesh_; }
		AssetID GetSprite() const { return sprite_; }
		AssetID GetText() const { return text_; }

		// 設定値の更新
		void SetMesh(AssetID id) { mesh_ = id; }
		void SetSprite(AssetID id) { sprite_ = id; }
		void SetText(AssetID id) { text_ = id; }

		// 未設定ならbuiltinデフォルトへフォールバックした実効値を返す
		AssetID GetMeshOrBuiltin() const;
		AssetID GetSpriteOrBuiltin() const;
		AssetID GetTextOrBuiltin() const;

		// シングルトン
		static DefaultMaterialSettings& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 各描画タイプのデフォルトマテリアルで未設定は空ID
		AssetID mesh_{};
		AssetID sprite_{};
		AssetID text_{};

		// 保存先の設定ファイルパス
		std::string configPath_{};
	};
} // Engine
