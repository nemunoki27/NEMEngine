#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <array>
#include <cstdint>

namespace Engine {

	//============================================================================
	//	MeshShaderSharedTypes structures
	//============================================================================
	struct SubMeshConstants {

		uint32_t indexOffset = 0;
		uint32_t indexCount = 0;
		uint32_t subMeshIndex = 0;
		uint32_t _pad0 = 0;
	};

	struct MeshDrawConstants {

		// MeshShader経路の総メッシュレット数
		uint32_t meshletCount = 0;
		// バッチ内のサブメッシュ数
		uint32_t subMeshCount = 0;
		// バッチ内のインスタンス数
		uint32_t instanceCount = 0;
		// フラスタムカリングを行うか
		uint32_t cullingEnabled = 0;
		// 圧縮頂点用のメッシュレットIndexバッファを使うか
		uint32_t packedMeshletVertexIndices = 0;
		uint32_t _reserved0 = 0;
		// 画面上の寄与が小さいメッシュレットを落とすか
		uint32_t contributionCullingEnabled = 0;
		// メッシュレット法線コーンによる背面判定を行うか
		uint32_t normalConeCullingEnabled = 0;
		// インスタンス単位カリングで使用するメッシュ全体Bounds
		Vector3 meshBoundsCenter = Vector3::AnyInit(0.0f);
		float meshBoundsRadius = 0.0f;
		// Contribution Cullingで消す最小ピクセル半径
		float contributionPixelThreshold = 1.0f;
		// 背面法アウトラインパスかどうかでカリングBoundsの安全側膨張に使う
		uint32_t invertedHullOutlinePass = 0;
		// アウトラインのモデル空間最大膨張量
		float outlineMaxModelExpansion = 0.0f;
		// Camera Z Offsetの最大絶対値
		float outlineMaxAbsCameraZOffset = 0.0f;
		// ScreenPixels幅を含むバッチかどうか
		uint32_t outlineHasScreenPixelWidth = 0;
		uint32_t _reserved1[3] = { 0, 0, 0 };

		// 連結Index/Meshletバッファ内の4段階LOD範囲
		std::array<uint32_t, kMeshLODCount> lodIndexOffsets{};
		std::array<uint32_t, kMeshLODCount> lodIndexCounts{};
		std::array<uint32_t, kMeshLODCount> lodMeshletOffsets{};
		std::array<uint32_t, kMeshLODCount> lodMeshletCounts{};
		// 投影半径が閾値以上ならLOD0/1/2を選び、それ未満はLOD3にする
		Vector3 lodPixelThresholds = Vector3(160.0f, 80.0f, 32.0f);
		uint32_t lodCount = kMeshLODCount;
	};
	static_assert(sizeof(MeshDrawConstants) % 16 == 0);

	// 背面法アウトラインのGPUデータ
	struct MeshOutlineGPUData {

		Color4 color = Color4::Black();

		float width = 0.0f;
		float cameraZOffset = 0.0f;
		uint32_t expansionMode = 0;
		uint32_t widthMode = 0;

		uint32_t bakedNormalTextureIndex = UINT32_MAX;
		uint32_t outlineSamplerTextureIndex = UINT32_MAX;
		uint32_t flags = 0;
		uint32_t _pad0 = 0;
	};
	static_assert(sizeof(MeshOutlineGPUData) % 16 == 0);

	// MeshOutlineGPUDataのflags
	static constexpr uint32_t kMeshOutlineFlagUseBakedNormal = 1u << 0;
	static constexpr uint32_t kMeshOutlineFlagUseOutlineSampler = 1u << 1;

	struct MeshSubMeshShaderData {

		uint32_t baseColorTextureIndex = UINT32_MAX;
		uint32_t normalTextureIndex = UINT32_MAX;
		uint32_t metallicRoughnessTextureIndex = UINT32_MAX;
		uint32_t emissiveTextureIndex = UINT32_MAX;

		uint32_t occlusionTextureIndex = UINT32_MAX;
		uint32_t specularTextureIndex = UINT32_MAX;
		float metallic = 0.0f;
		float roughness = 0.5f;

		// サブメッシュごとのローカル行列(位置・Bounds・Culling用)
		Matrix4x4 localMatrix = Matrix4x4::Identity();
		// localMatrixの法線変換行列transpose(inverse(localMatrix))
		// 非一様スケールでも法線が壊れないよう、位置用とは別に持つ
		Matrix4x4 localNormalMatrix = Matrix4x4::Identity();

		// インポート時の色
		Color4 importedBaseColor = Color4::White();
		// エディタ編集色
		Color4 color = Color4::White();
		// 発光色
		Color4 emissiveColor = Color4(0.0f, 0.0f, 0.0f, 0.0f);
		// サブメッシュごとのUV
		Matrix4x4 uvMatrix = Matrix4x4::Identity();

		// Position Scaling膨張の基準ピボット(モデル空間)
		Vector3 sourcePivot = Vector3::AnyInit(0.0f);
		// localMatrixの線形部の行列式の符号で負スケールのmirror時に-1
		float localOrientationSign = 1.0f;
	};
	static_assert(sizeof(MeshSubMeshShaderData) == 288,
		"MeshSubMeshShaderData must match HLSL SubMeshShaderData layout");
	static_assert(sizeof(MeshSubMeshShaderData) % 16 == 0);
} // Engine
