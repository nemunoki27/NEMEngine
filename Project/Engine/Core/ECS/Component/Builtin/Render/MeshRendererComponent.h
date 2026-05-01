#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Graphics/DxLib/DxStructures.h>
#include <Engine/Core/Asset/AssetTypes.h>
#include <Engine/Core/UUID/UUID.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Matrix4x4.h>

namespace Engine {

	//============================================================================
	//	MeshRendererComponent struct
	//============================================================================

	struct MeshSubMeshTextureOverride {

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
		std::string queue = "Opaque";

		// Zプリパスを有効にするか
		bool enableZPrepass = true;

		// サブメッシュごとのテクスチャ設定
		std::vector<MeshSubMeshTextureOverride> subMeshes{};
	};

	// json変換
	void from_json(const nlohmann::json& in, MeshSubMeshTextureOverride& overrideData);
	void to_json(nlohmann::json& out, const MeshSubMeshTextureOverride& overrideData);
	void from_json(const nlohmann::json& in, MeshRendererComponent& component);
	void to_json(nlohmann::json& out, const MeshRendererComponent& component);

	// helpers
	namespace MeshSubMeshRuntime {

		// 行列の構築
		Matrix4x4 BuildUVMatrix(const MeshSubMeshTextureOverride& subMesh);
		Matrix4x4 BuildLocalMatrix(const MeshSubMeshTextureOverride& subMesh);
		// ギズモ用のピボットを考慮したローカル行列
		Matrix4x4 BuildGizmoLocalMatrix(const MeshSubMeshTextureOverride& subMesh);
		Matrix4x4 BuildRenderLocalMatrix(const MeshSubMeshTextureOverride& subMesh);

		// ランタイム更新
		void UpdateSubMeshRuntime(MeshSubMeshTextureOverride& subMesh, const Matrix4x4& parentWorldMatrix);
		void UpdateRendererRuntime(MeshRendererComponent& renderer, const Matrix4x4& parentWorldMatrix);
	}

	ENGINE_REGISTER_COMPONENT(MeshRendererComponent, "MeshRenderer");
} // Engine