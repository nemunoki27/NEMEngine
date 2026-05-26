#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Types.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPhase.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

namespace Engine {

	//============================================================================
	//	MeshRendererComponent struct
	//============================================================================

	struct SubMeshMaterial {

		// 表示用の名前
		std::string name;

		// 元のサブメッシュインデックス
		UUID stableID{};
		// 元メッシュ内でのインデックス
		uint32_t sourceSubMeshIndex = 0;

		// 設定するテクスチャ(読みこまれた時点で設定されていればそのテクスチャが設定される)
		AssetID baseColorTexture{};
		AssetID normalTexture{};
		AssetID metallicRoughnessTexture{};
		AssetID specularTexture{};
		AssetID emissiveTexture{};
		AssetID occlusionTexture{};

		// サブメッシュパラメータ
		// 色
		Color4 color = Color4::White();
		// 発光色
		Color4 emissiveColor = Color4(0.0f, 0.0f, 0.0f, 0.0f);

		// PBRパラメータ
		float metallic = 0.0f;
		float roughness = 0.5f;

		// UV
		Vector2 uvPos = Vector2::AnyInit(0.0f);
		float uvRotation = 0.0f;
		Vector2 uvScale = Vector2::AnyInit(1.0f);
		// ランタイム計算結果
		Matrix4x4 uvMatrix = Matrix4x4::Identity();

		// ローカル変換(Entityが親)
		Vector3 localPos = Vector3::AnyInit(0.0f);
		Vector3 localRotation = Vector3::AnyInit(0.0f);
		Vector3 localScale = Vector3::AnyInit(1.0f);

		// ランタイム計算結果
		Matrix4x4 worldMatrix = Matrix4x4::Identity();

		// 頂点座標から計算したピボット
		Vector3 sourcePivot = Vector3::AnyInit(0.0f);
	};

	// メッシュ描画
	struct MeshRendererComponent {

		// メッシュ
		AssetID mesh{};
		// マテリアル
		AssetID material{};

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

		// Zプリパスを有効にするか
		bool enableZPrepass = true;

		// サブメッシュごとのマテリアル設定
		std::vector<SubMeshMaterial> subMeshes{};
	};

	// json変換
	void from_json(const nlohmann::json& in, SubMeshMaterial& subMeshMaterial);
	void to_json(nlohmann::json& out, const SubMeshMaterial& subMeshMaterial);
	void from_json(const nlohmann::json& in, MeshRendererComponent& component);
	void to_json(nlohmann::json& out, const MeshRendererComponent& component);

	// helpers
	namespace MeshSubMeshRuntime {

		// 行列の構築
		Matrix4x4 BuildUVMatrix(const SubMeshMaterial& subMesh);
		Matrix4x4 BuildLocalMatrix(const SubMeshMaterial& subMesh);
		// ギズモ用のピボットを考慮したローカル行列
		Matrix4x4 BuildGizmoLocalMatrix(const SubMeshMaterial& subMesh);
		Matrix4x4 BuildRenderLocalMatrix(const SubMeshMaterial& subMesh);

		// ランタイム更新
		void UpdateSubMeshRuntime(SubMeshMaterial& subMesh, const Matrix4x4& parentWorldMatrix);
		void UpdateRendererRuntime(MeshRendererComponent& renderer, const Matrix4x4& parentWorldMatrix);
	}

	ENGINE_REGISTER_COMPONENT(MeshRendererComponent, "MeshRenderer");
} // Engine
