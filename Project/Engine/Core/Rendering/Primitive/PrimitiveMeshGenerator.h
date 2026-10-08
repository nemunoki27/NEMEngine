#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <array>
#include <cstdint>
#include <vector>

namespace Engine {

	// front
	struct PrimitiveCrossPlaneParams;
	struct PrimitiveCubeParams;
	struct PrimitiveCylinderParams;
	struct PrimitiveHemisphereParams;
	struct PrimitivePlaneParams;
	struct PrimitiveRendererComponent;
	struct PrimitiveRingParams;
	struct PrimitiveSphereParams;

	//============================================================================
	//	PrimitiveMeshGenerator structures
	//============================================================================
	// プロシージャル生成した1頂点
	struct PrimitiveMeshVertex {

		Vector3 position;
		Vector3 normal;
		Vector2 texcoord;
		Vector3 tangent;
		// UVから求めた従法線の向き
		float tangentSign = 1.0f;
	};

	// 生成したメッシュ、VS描画とBLAS構築の両方で使う
	struct PrimitiveMeshData {

		std::vector<PrimitiveMeshVertex> vertices;
		std::vector<uint32_t> indices;
	};

	// 形状生成へ使う値だけを正規化して保持するcacheキー
	struct PrimitiveGeometryKey {

		std::array<uint32_t, 12> values{};
		uint8_t valueCount = 0;

		bool operator==(const PrimitiveGeometryKey&) const = default;
	};

	//============================================================================
	//	PrimitiveMeshGenerator class
	//	PrimitiveTypeとパラメータからCPU上に頂点とインデックスを生成する
	//============================================================================
	class PrimitiveMeshGenerator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// componentのtypeに応じて形状を生成する
		static void Generate(const PrimitiveRendererComponent& renderer, PrimitiveMeshData& out);

		// 形状とパラメータからジオメトリの一意性を表すハッシュを求める
		static uint64_t ComputeHash(const PrimitiveRendererComponent& renderer);

		// 形状生成へ使う値を正規化したcacheキーを求める
		static PrimitiveGeometryKey ComputeKey(const PrimitiveRendererComponent& renderer);

		// cacheキーのハッシュを求める
		static uint64_t ComputeHash(const PrimitiveGeometryKey& key);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 生成とcache識別に同じ分割数を使う
		static int32_t ClampDivide(int32_t value, int32_t minimum);
		static int32_t ClampCylinderHeightDivide(int32_t value);

		// 形状ごとの生成
		static void GeneratePlane(const PrimitivePlaneParams& params, PrimitiveMeshData& out);
		static void GenerateCrossPlane(const PrimitiveCrossPlaneParams& params, PrimitiveMeshData& out);
		static void GenerateRing(const PrimitiveRingParams& params, PrimitiveMeshData& out);
		static void GenerateCylinder(const PrimitiveCylinderParams& params, PrimitiveMeshData& out);
		static void GenerateSphere(const PrimitiveSphereParams& params, PrimitiveMeshData& out);
		static void GenerateHemisphere(const PrimitiveHemisphereParams& params, PrimitiveMeshData& out);
		static void GenerateCube(const PrimitiveCubeParams& params, PrimitiveMeshData& out);

		// 位置とUVから頂点ごとの接線を計算する、法線マップのTBN構築に使う
		static void ComputeTangents(PrimitiveMeshData& out);
	};
} // Engine
