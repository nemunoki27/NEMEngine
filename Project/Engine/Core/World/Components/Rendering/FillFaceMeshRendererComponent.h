#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	FillMeshRendererComponent struct
	//============================================================================
	// 座標を受け取って、メッシュ面を構築して描画
	struct FillMeshRendererComponent {

		// メッシュ構築フラグ、trueのフレームで構築
		bool buildMesh = false;

		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides{};

		// 面を構築するローカル座標、XZ平面
		std::vector<Vector3> facePositions{};

		// 色
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
		RenderPhase queue = RenderPhase::Opaque;

		// Systemが作る三角形分割済みインデックス、保存しない
		std::vector<uint32_t> triangleIndices{};
		// ジオメトリ更新世代、GPU側の再構築判定に使う、保存しない
		uint32_t geometryGeneration = 0;
	};

	// json変換
	void from_json(const nlohmann::json& in, FillMeshRendererComponent& component);
	void to_json(nlohmann::json& out, const FillMeshRendererComponent& component);

} // Engine