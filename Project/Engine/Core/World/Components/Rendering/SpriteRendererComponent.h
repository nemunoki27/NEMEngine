#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	SpriteRendererComponent struct
	//============================================================================
	// スプライト描画
	struct SpriteRendererComponent {

		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels =
			ComponentChangeChannel::Render;

		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ
		MaterialInstanceParameters materialInstance{};

		// サイズ
		Vector2 size = Vector2::AnyInit(32.0f);
		// ピボット、0.0～1.0の範囲で指定、0.5, 0.5が中心
		Vector2 pivot = Vector2::AnyInit(0.5f);

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
	};

	// json変換
	void from_json(const nlohmann::json& in, SpriteRendererComponent& component);
	void to_json(nlohmann::json& out, const SpriteRendererComponent& component);

} // Engine
