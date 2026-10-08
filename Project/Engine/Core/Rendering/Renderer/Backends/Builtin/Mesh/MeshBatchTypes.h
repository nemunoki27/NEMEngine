#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	MeshViewConstants struct
	//	描画とカリングとLODのCamera転送型
	//============================================================================
	struct MeshViewConstants {

		// 実際に描画するビューの行列
		Matrix4x4 viewProjection = Matrix4x4::Identity();
		Matrix4x4 previousViewProjection = Matrix4x4::Identity();
		// カリング対象のView行列
		Matrix4x4 cullingViewProjection = Matrix4x4::Identity();
		// カリングCameraのView空間へ変換
		Matrix4x4 cullingView = Matrix4x4::Identity();
		// NormalCone判定のCamera位置
		Vector3 cullingCameraPos = Vector3::AnyInit(0.0f);
		// Nearを跨ぐBoundsの省略を避ける
		float cullingNearClip = 0.001f;
		// 遮蔽判定のCamera前方
		Vector3 cullingCameraForward = Vector3(0.0f, 0.0f, 1.0f);
		float _cullingPad0 = 0.0f;
		// 描画先Viewportサイズ
		Vector2 viewSize = Vector2::AnyInit(1.0f);
		// カリング対象のViewportサイズ
		Vector2 cullingViewSize = Vector2::AnyInit(1.0f);
		// 投影行列のXY倍率
		Vector2 cullingProjectionScale = Vector2::AnyInit(1.0f);
		Vector2 _pad0 = Vector2::AnyInit(0.0f);
		// 照明計算に使う描画Camera位置
		Vector3 renderCameraPos = Vector3::AnyInit(0.0f);
		uint32_t frameSerial = 0;
		// LODは各描画先のカメラで判定する
		Matrix4x4 lodView = Matrix4x4::Identity();
		Vector2 lodProjectionScale = Vector2::AnyInit(1.0f);
		float lodNearClip = 0.001f;
		uint32_t lodOrthographic = 0;
	};
	static_assert(sizeof(MeshViewConstants) % 16 == 0);

	//============================================================================
	//	MeshIndirectArgsConstants struct
	//	間接描画のIndex数を渡す転送型
	//============================================================================
	struct MeshIndirectArgsConstants {

		// 間接描画に渡すIndex数
		uint32_t indexCount = 0;
		uint32_t _pad[3] = { 0, 0, 0 };
	};
	static_assert(sizeof(MeshIndirectArgsConstants) % 16 == 0);

	//============================================================================
	//	MeshInstanceData struct
	//	Meshの配置と描画状態を渡す転送型
	//============================================================================
	struct MeshInstanceData {

		// Entityの配置とカリングに使うワールド行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Matrix4x4 previousWorldMatrix = Matrix4x4::Identity();
		// 非一様スケールと負スケールに対応する法線行列
		Matrix4x4 normalMatrix = Matrix4x4::Identity();

		// このインスタンスのサブメッシュ配列先頭
		uint32_t subMeshDataOffset = 0;
		uint32_t subMeshCount = 0;

		// スキニングするか
		uint32_t flags = 0;
		// スキニングする場合の、スキン頂点配列のオフセット
		uint32_t skinnedVertexOffset = 0;

		// このインスタンスが参照するアウトラインGPUデータのインデックス
		uint32_t outlineDataIndex = 0;
		// 負スケールによる面の反転を記録
		float orientationSign = 1.0f;
		// ラスターピックでEntityを識別
		uint32_t entityIndex = UINT32_MAX;
		uint32_t entityGeneration = UINT32_MAX;

		// インスタンスごとの乗算色
		Color4 color = Color4::White();
		uint32_t motionFrameSerial = 0;
		uint32_t _motionPad[3] = { 0, 0, 0 };
	};
	static_assert(sizeof(MeshInstanceData) % 16 == 0);

	// Skinning済みの頂点を使用
	static constexpr uint32_t kMeshInstanceFlagSkinned = 1u;
	// PixelShaderへ渡す描画機能のフラグ
	static constexpr uint32_t kMeshInstanceFlagLighting = 1u << 1;
	static constexpr uint32_t kMeshInstanceFlagReceiveShadow = 1u << 2;
	static constexpr uint32_t kMeshInstanceFlagReceiveIBL = 1u << 3;
	static constexpr uint32_t kMeshInstanceFlagReceiveReflection = 1u << 4;
	static constexpr uint32_t kMeshInstanceFlagLODDither = 1u << 5;
}
