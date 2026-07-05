#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <cstdint>
#include <vector>

namespace Engine {

	//============================================================================
	//	PrimitiveMeshGenerator structures
	//============================================================================
	// プロシージャル生成した1頂点
	struct PrimitiveMeshVertex {

		Vector3 position;
		Vector3 normal;
		Vector2 texcoord;
		Vector3 tangent;
	};

	// 生成したメッシュ、VS描画とBLAS構築の両方で使う
	struct PrimitiveMeshData {

		std::vector<PrimitiveMeshVertex> vertices;
		std::vector<uint32_t> indices;
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
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

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
