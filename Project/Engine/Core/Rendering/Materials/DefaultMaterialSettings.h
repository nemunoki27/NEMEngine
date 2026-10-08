#pragma once

//============================================================================
//	include
//============================================================================
#include "DefaultMaterialConfiguration.h"

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	DefaultMaterialSettings class
	//	描画種別ごとの既定Materialを保持して保存するクラス
	//============================================================================
	class DefaultMaterialSettings {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DefaultMaterialSettings() = default;
		~DefaultMaterialSettings() = default;

		// 保存先を記憶し、未作成なら空の設定を読み込む
		bool Load(const std::string& configPath);
		// 現在の設定を保存し、成功したか返す
		bool Save() const;

		//--------- accessor -----------------------------------------------------

		// 設定値の取得、未設定なら空IDを返す
		AssetID GetMesh() const { return configuration_.mesh; }
		AssetID GetSprite() const { return configuration_.sprite; }
		AssetID GetText() const { return configuration_.text; }
		AssetID GetLine() const { return configuration_.line; }
		AssetID GetPrimitive() const { return configuration_.primitive; }
		AssetID GetPrimitive2D() const { return configuration_.primitive2D; }
		AssetID GetRaytracingReflection() const { return configuration_.raytracingReflection; }

		// 設定値の更新
		void SetMesh(AssetID id) { configuration_.mesh = id; }
		void SetSprite(AssetID id) { configuration_.sprite = id; }
		void SetText(AssetID id) { configuration_.text = id; }
		void SetLine(AssetID id) { configuration_.line = id; }
		void SetPrimitive(AssetID id) { configuration_.primitive = id; }
		void SetPrimitive2D(AssetID id) { configuration_.primitive2D = id; }
		void SetRaytracingReflection(AssetID id) { configuration_.raytracingReflection = id; }

		// 未設定ならbuiltinデフォルトへフォールバックした実効値を返す
		AssetID GetMeshOrBuiltin() const;
		AssetID GetSpriteOrBuiltin() const;
		AssetID GetTextOrBuiltin() const;
		AssetID GetLineOrBuiltin() const;
		AssetID GetPrimitiveOrBuiltin() const;
		AssetID GetPrimitive2DOrBuiltin() const;
		AssetID GetRaytracingReflectionOrBuiltin() const;

		// シングルトン
		static DefaultMaterialSettings& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 各描画タイプのデフォルトマテリアルで未設定は空ID
		DefaultMaterialConfiguration configuration_;

		// 保存先の設定ファイルパス
		std::string configPath_{};
	};
} // Engine
