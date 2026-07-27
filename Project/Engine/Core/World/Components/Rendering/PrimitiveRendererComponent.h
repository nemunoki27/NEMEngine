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
	// 分割数の上限、頂点バッファが青天井に膨れないよう生成側とUIで共有する
	inline constexpr int32_t kMaxPrimitiveDivide = 256;

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

	// 描画空間、Plane/Ringのみ2D描画に切り替えられる
	enum class PrimitiveRenderSpace : uint8_t {

		World3D,
		Screen2D,
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
		// 円弧の開始角と終了角、度数法で0〜360で全周
		float startAngle = 0.0f;
		float endAngle = 360.0f;
		// 円周方向の分割数
		int32_t divide = 16;
	};

	struct PrimitiveCylinderParams {

		// 上面と中心と下面の半径
		float topRadius = 1.0f;
		float centerRadius = 1.0f;
		float bottomRadius = 1.0f;
		// 上面と下面の半径へ引き寄せる強さ
		float topRadiusWeight = 0.0f;
		float bottomRadiusWeight = 0.0f;
		// 上面と中心と下面の色
		Color4 topColor = Color4::White();
		Color4 centerColor = Color4::White();
		Color4 bottomColor = Color4::White();
		// 高さ
		float height = 2.0f;
		// 展開角、度数法で360で全周
		float maxAngle = 360.0f;
		// 円周方向と高さ方向の分割数
		int32_t radialDivide = 16;
		int32_t heightDivide = 8;
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
		// 描画空間、Plane/Ringのみ2D描画に切り替えられる
		PrimitiveRenderSpace renderSpace = PrimitiveRenderSpace::World3D;

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
		MaterialParameterOverrides parameterOverrides{};

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

	// Plane/Ringかつ描画空間がScreen2Dのときだけ2D描画になる
	inline bool IsPrimitiveScreen2D(const PrimitiveRendererComponent& component) {

		return component.renderSpace == PrimitiveRenderSpace::Screen2D &&
			(component.type == PrimitiveType::Plane || component.type == PrimitiveType::Ring);
	}

	// 形状パラメータのjson変換、コンポーネントとエフェクトアセットで共用する
	void from_json(const nlohmann::json& in, PrimitivePlaneParams& params);
	void to_json(nlohmann::json& out, const PrimitivePlaneParams& params);
	void from_json(const nlohmann::json& in, PrimitiveCrossPlaneParams& params);
	void to_json(nlohmann::json& out, const PrimitiveCrossPlaneParams& params);
	void from_json(const nlohmann::json& in, PrimitiveRingParams& params);
	void to_json(nlohmann::json& out, const PrimitiveRingParams& params);
	void from_json(const nlohmann::json& in, PrimitiveCylinderParams& params);
	void to_json(nlohmann::json& out, const PrimitiveCylinderParams& params);
	void from_json(const nlohmann::json& in, PrimitiveSphereParams& params);
	void to_json(nlohmann::json& out, const PrimitiveSphereParams& params);
	void from_json(const nlohmann::json& in, PrimitiveHemisphereParams& params);
	void to_json(nlohmann::json& out, const PrimitiveHemisphereParams& params);
	void from_json(const nlohmann::json& in, PrimitiveCubeParams& params);
	void to_json(nlohmann::json& out, const PrimitiveCubeParams& params);

	// json変換
	void from_json(const nlohmann::json& in, PrimitiveRendererComponent& component);
	void to_json(nlohmann::json& out, const PrimitiveRendererComponent& component);

} // Engine
