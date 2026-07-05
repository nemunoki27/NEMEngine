#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <cstdint>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	PrimitiveRendererComponent struct
	//============================================================================
	// プロシージャルに生成する形状の種類
	enum class PrimitiveType : uint32_t {

		Plane,
		CrossPlane,
		Ring,
		Cylinder,
		Sphere,
		Hemisphere,
		Cube,
	};

	// 平面を張る軸
	enum class PrimitivePlaneAxis : uint8_t {

		XY,
		XZ,
		YZ,
	};

	// 円柱のフタの付け方
	enum class PrimitiveCylinderCap : uint8_t {

		None,
		Top,
		Bottom,
		Both,
	};

	// 円柱のUV展開方法
	enum class PrimitiveCylinderUVMode : uint8_t {

		None,
		Radial,
	};

	struct PrimitivePlaneParams {

		// 平面の大きさ
		Vector2 size = Vector2::AnyInit(1.0f);
		// 中心とする基準点
		Vector2 pivot = Vector2::AnyInit(0.5f);
		// 張る軸
		PrimitivePlaneAxis axis = PrimitivePlaneAxis::XY;
		// 各方向の分割数
		int32_t divideX = 1;
		int32_t divideY = 1;
	};

	struct PrimitiveCrossPlaneParams {

		// 平面の大きさ
		Vector2 size = Vector2::AnyInit(1.0f);
		// 中心とする基準点
		Vector2 pivot = Vector2::AnyInit(0.5f);
		// 交差させる枚数
		int32_t planeCount = 2;
	};

	struct PrimitiveRingParams {

		// 外周半径
		float outerRadius = 1.0f;
		// 内周半径
		float innerRadius = 0.5f;
		// 円周方向の分割数
		int32_t divide = 16;
	};

	struct PrimitiveCylinderParams {

		// 上面と下面の半径
		float topRadius = 1.0f;
		float bottomRadius = 1.0f;
		// 高さ
		float height = 2.0f;
		// 展開角、2πで全周
		float maxAngle = std::numbers::pi_v<float> * 2.0f;
		// 円周方向と高さ方向の分割数
		int32_t radialDivide = 16;
		int32_t heightDivide = 1;
		// フタの付け方
		PrimitiveCylinderCap cap = PrimitiveCylinderCap::Both;
		// UV展開方法
		PrimitiveCylinderUVMode uvMode = PrimitiveCylinderUVMode::None;
	};

	struct PrimitiveSphereParams {

		// 半径
		float radius = 1.0f;
		// 経度方向と緯度方向の分割数
		int32_t longitudeDivide = 24;
		int32_t latitudeDivide = 16;
	};

	struct PrimitiveHemisphereParams {

		// 半径
		float radius = 1.0f;
		// 経度方向と緯度方向の分割数
		int32_t longitudeDivide = 24;
		int32_t latitudeDivide = 8;
		// 底面のフタを付けるか
		bool bottomCap = true;
	};

	struct PrimitiveCubeParams {

		// 各辺の大きさ
		Vector3 size = Vector3::AnyInit(1.0f);
		// 中心とする基準点、0.5で中央
		Vector3 pivot = Vector3::AnyInit(0.5f);
	};

	// プロシージャル形状を生成して描画
	struct PrimitiveRendererComponent {

		// 描画する形状
		PrimitiveType type = PrimitiveType::Plane;

		// 形状ごとのパラメータ、切り替えで失わないよう全形状分を保持する
		PrimitivePlaneParams plane{};
		PrimitiveCrossPlaneParams crossPlane{};
		PrimitiveRingParams ring{};
		PrimitiveCylinderParams cylinder{};
		PrimitiveSphereParams sphere{};
		PrimitiveHemisphereParams hemisphere{};
		PrimitiveCubeParams cube{};

		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides{};

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

		// ライティングや影の適用を切り替えるフラグ
		MeshRenderFlags renderFlags = MeshRenderFlags::Default;
	};

	// json変換
	void from_json(const nlohmann::json& in, PrimitiveRendererComponent& component);
	void to_json(nlohmann::json& out, const PrimitiveRendererComponent& component);

	ENGINE_REGISTER_COMPONENT(PrimitiveRendererComponent, "PrimitiveRenderer");
} // Engine
