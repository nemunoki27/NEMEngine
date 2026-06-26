#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPhase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	FillMeshRendererComponent struct
	//============================================================================
	// 座標を受け取って、メッシュ面を構築して描画
	class FillMeshRendererComponent {

		// マテリアル
		AssetID material{};

		// 面を構築するワールド座標
		std::vector<Vector3> facePostions{};

		// とりあえず色だけ
		Color4 color = Color4::White();

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
	void from_json(const nlohmann::json& in, FillMeshRendererComponent& component);
	void to_json(nlohmann::json& out, const FillMeshRendererComponent& component);

	ENGINE_REGISTER_COMPONENT(FillMeshRendererComponent, "FillMeshRenderer");
} // Engine