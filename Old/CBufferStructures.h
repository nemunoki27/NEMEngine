#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Vector4.h>
#include <Engine/MathLib/Quaternion.h>
#include <Engine/MathLib/Matrix4x4.h>

// c++
#include <cstdint>
#include <string>

namespace SakuEngine {

	// front
	class BaseTransform3D;

	//============================================================================
	//	CBufferStructures
	//============================================================================

	//----------------------------------------------------------------------------
	//	TransformationMatrix
	//	ワールド行列/逆転置行列など、描画に必要な行列を保持・更新する。
	//----------------------------------------------------------------------------
	struct TransformationMatrix {

		Matrix4x4 world;
		Matrix4x4 worldInverseTranspose;

		void Update(const BaseTransform3D* parent, const Vector3& scale,
			const Quaternion& rotation, const Vector3& translation, bool isIgnoreParentScale,
			const std::optional<Matrix4x4>& billboardMatrix = std::nullopt);
	};

	//----------------------------------------------------------------------------
	//	MaterialForGPU
	//	ベースカラー/発光/UV変換等、マテリアルのGPU定数をまとめて保持する。
	//----------------------------------------------------------------------------
	struct MaterialForGPU {

		Color color;

		uint32_t textureIndex;
		uint32_t normalMapTextureIndex;
		int32_t enableNormalMap;
		int32_t enableDithering;

		float emissiveIntensity;
		Vector3 emissionColor;

		Matrix4x4 uvTransform;

		uint32_t postProcessMask;
		uint32_t isRejection;
	};

	//----------------------------------------------------------------------------
	//	LightingForGPU
	//	ライティングに必要な共通定数(環境/各ライト関連)を保持する。
	//----------------------------------------------------------------------------
	struct LightingForGPU {

		int32_t enableLighting;
		int32_t enableHalfLambert;
		int32_t enableBlinnPhongReflection;
		int32_t enableImageBasedLighting;
		int32_t castShadow;

		float phongRefShininess;
		Vector3 specularColor;

		float shadowRate;
		float environmentCoefficient;
	};

	//----------------------------------------------------------------------------
	//	SpriteMaterialForGPU
	//	スプライト用の色/UV/しきい値等、GPU定数を保持する。
	//----------------------------------------------------------------------------
	struct SpriteMaterialForGPU {

		Matrix4x4 uvTransform;
		Color color;
		Vector3 emissionColor;
		int32_t useVertexColor;
		int32_t useAlphaColor;
		float emissiveIntensity;
		float alphaReference;

		// 適応するポストエフェクトのビット
		uint32_t postProcessMask;

		void Init();
		void ImGui();
	};

	//----------------------------------------------------------------------------
	//	SpriteMaterialForGPU
	//	スプライト用の色/UV/しきい値等、GPU定数を保持する。
	//----------------------------------------------------------------------------
	struct MSDFTextMaterialForGPU {

		Color color;
		Color outlineColor;

		Vector2 atlasSize;
		float pixelRange;
		float outlineWidth;

		float softness;
		float boldness;
		uint32_t enableOutline;
		// 適応するポストエフェクトのビット
		uint32_t postProcessMask;

		void Init();
		void ImGui();
	};
}; // SakuEngine