#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Math/Color.h>
#include <Engine/Core/Math/Matrix4x4.h>
#include <Engine/Core/Math/Vector3.h>

// c++
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
		uint32_t _pad[3] = { 0, 0, 0 };
	};

	struct MeshSubMeshShaderData {

		uint32_t baseColorTextureIndex = 0;
		float _pad[3] = { 0.0f, 0.0f, 0.0f };

		// サブメッシュごとのローカル行列
		Matrix4x4 localMatrix = Matrix4x4::Identity();

		// インポート時の色
		Color4 importedBaseColor = Color4::White();
		// エディタ編集色
		Color4 color = Color4::White();
		// サブメッシュごとのUV
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};
} // Engine
