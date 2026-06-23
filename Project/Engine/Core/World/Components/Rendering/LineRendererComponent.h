#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPhase.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	LineRendererComponent struct
	//============================================================================
	// ライン描画、A→B→C…と連続したポリラインを1コンポーネントで持つ
	struct LineRendererComponent {

		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ上書き
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides{};

		// ポリラインの点列、隣り合う点を線分でつなぐ
		std::vector<LinePoint> points{};
		// 始点と終点をつないで閉じるか
		bool loop = false;

		// 2D描画か、trueなら正射影カメラでxyのみ使う
		bool is2D = false;
		// 点をワールド絶対座標として扱うか、falseなら親Transformに追従する
		bool useWorldSpace = true;
		// 親エンティティのlocalFileID、0なら自身のTransformを親にする。useWorldSpace=falseで有効
		UUID parentLocalFileID{};
		// 親のスケールを無視するか
		bool ignoreParentScale = false;
		// 親の回転を無視するか
		bool ignoreParentRotation = false;

		// 描画レイヤー
		int32_t layer = 0;
		// 描画レイヤー内の中での順序
		int32_t order = 0;
		// 表示フラグ
		bool visible = true;

		// ブレンドモード
		BlendMode blendMode = BlendMode::Normal;
		// 描画キュー
		RenderPhase queue = RenderPhase::Transparent;
	};

	// json変換
	void from_json(const nlohmann::json& in, LineRendererComponent& component);
	void to_json(nlohmann::json& out, const LineRendererComponent& component);

	ENGINE_REGISTER_COMPONENT(LineRendererComponent, "LineRenderer");
} // Engine
