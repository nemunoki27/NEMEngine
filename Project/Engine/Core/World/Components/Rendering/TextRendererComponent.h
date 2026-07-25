#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	TextRendererComponent struct
	//============================================================================
	// 文字レイアウト済みの1グリフデータ
	struct TextLayoutGlyph {

		Vector2 rectMin{};
		Vector2 rectMax{};
		Vector2 uvMin{};
		Vector2 uvMax{};
	};
	// 文字ごとのトランスフォーム、グリフ中心を基準にSRTを掛ける
	struct TextCharTransform {

		Vector2 translation{};
		// Z回転(度)
		float rotation = 0.0f;
		Vector2 scale = Vector2::AnyInit(1.0f);
	};
	// ランタイムキャッシュデータ
	struct TextLayoutRuntime {

		// フォント
		AssetID font{};
		// 描画するテキスト
		std::string text{};
		
		// フォントサイズ
		float fontSize = 32.0f;
		// 文字間隔
		float charSpacing = 0.0f;

		// 描画時に使う情報
		Vector2 atlasSize = Vector2::AnyInit(1.0f);
		float pxRange = 8.0f;
		std::vector<TextLayoutGlyph> glyphs{};
		// テキストブロック全体のサイズ、ピボット適用の基準に使う
		Vector2 boundsSize = Vector2::AnyInit(0.0f);

		// キャッシュが有効か
		bool valid = false;
	};
	// テキスト描画
	struct TextRendererComponent {

		// フォント設定
		AssetID font{};
		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ上書き、reflection駆動で描画/アニメーションに使う
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides{};
		// 描画するテキスト
		std::string text = "Text";

		// フォントサイズ
		float fontSize = 32.0f;
		// 文字間隔
		float charSpacing = 0.0f;
		// ピボット、テキストブロックを正規化した0-1基準でこの点が原点に合う、スプライトと同じ扱い
		Vector2 pivot = Vector2::AnyInit(0.0f);
		// UVを文字ごとの0-1で扱うか
		bool uvPerCharacter = true;

		// 文字ごとのトランスフォーム、描画グリフ数に合わせて伸縮する
		std::vector<TextCharTransform> charTransforms{};

		// 描画レイヤー
		int32_t layer = 0;
		// 描画レイヤー内の中での順序
		int32_t order = 0;
		// 表示フラグ
		bool visible = true;

		// ブレンドモード
		BlendMode blendMode = BlendMode::Normal;
		// 描画キュー
		RenderPhase queue = RenderPhase::ScreenUI;

		// 2D(スクリーン空間)か3D(ワールド空間)かの次元
		Dimension dimension = Dimension::Type2D;
		// 3D描画時にピクセル単位のグリフをワールド単位へ縮小するスケール
		float worldScale = 0.01f;

		// ランタイム用の文字レイアウトキャッシュ
		TextLayoutRuntime runtimeLayout{};
	};

	// json変換
	void from_json(const nlohmann::json& in, TextRendererComponent& component);
	void to_json(nlohmann::json& out, const TextRendererComponent& component);

} // Engine
