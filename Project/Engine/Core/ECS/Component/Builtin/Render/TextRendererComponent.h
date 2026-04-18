#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Graphics/DxLib/DxStructures.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector4.h>

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

		// キャッシュが有効か
		bool valid = false;
	};
	// テキスト描画
	struct TextRendererComponent {

		// フォント設定
		AssetID font{};
		// マテリアル
		AssetID material{};
		// 描画するテキスト
		std::string text = "Text";

		// フォントサイズ
		float fontSize = 32.0f;
		// 文字間隔
		float charSpacing = 0.0f;

		// 色
		Color color = Color::White();

		// 描画レイヤー
		int32_t layer = 0;
		// 描画レイヤー内の中での順序
		int32_t order = 0;
		// 表示フラグ
		bool visible = true;

		// ブレンドモード
		BlendMode blendMode = BlendMode::Normal;
		// 描画キュー
		std::string queue = "CanvasPreModel";

		// ランタイム用の文字レイアウトキャッシュ
		TextLayoutRuntime runtimeLayout{};
	};

	// json変換
	void from_json(const nlohmann::json& in, TextRendererComponent& component);
	void to_json(nlohmann::json& out, const TextRendererComponent& component);

	ENGINE_REGISTER_COMPONENT(TextRendererComponent, "TextRenderer");
} // Engine