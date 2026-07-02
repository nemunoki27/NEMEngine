#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	MeshRendererComponent struct
	//============================================================================
	// ライティングや影の適用をビット単位で切り替えるフラグ
	enum class MeshRenderFlags : uint32_t {

		None = 0,
		// ライティングを行うか、無効ならUnlitでアルベドと発光をそのまま出す
		Lighting = 1 << 0,
		// 他のメッシュへ影を落とすか
		CastShadow = 1 << 1,
		// 平行光源の影を受けるか
		ReceiveShadow = 1 << 2,
		// SkyboxのIBL環境光を受けるか
		ReceiveIBL = 1 << 3,
		// 他のメッシュの反射に映るか
		CastReflection = 1 << 4,
		// レイトレ反射を受けるか
		ReceiveReflection = 1 << 5,
		// 全フラグ有効、追加時はここにも足す
		Default = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5),
	};

	inline MeshRenderFlags operator|(MeshRenderFlags lhs, MeshRenderFlags rhs) {

		return static_cast<MeshRenderFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
	}

	inline bool HasMeshRenderFlag(MeshRenderFlags flags, MeshRenderFlags target) {

		return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(target)) != 0;
	}

	inline void SetMeshRenderFlag(MeshRenderFlags& flags, MeshRenderFlags target, bool enabled) {

		if (enabled) {
			flags = static_cast<MeshRenderFlags>(static_cast<uint32_t>(flags) | static_cast<uint32_t>(target));
		} else {
			flags = static_cast<MeshRenderFlags>(static_cast<uint32_t>(flags) & ~static_cast<uint32_t>(target));
		}
	}

	struct SubMeshMaterial {

		// 表示用の名前
		std::string name;

		// 元のサブメッシュインデックス
		UUID stableID{};
		// 元メッシュ内でのインデックス
		uint32_t sourceSubMeshIndex = 0;

		// シェーダーごとのマテリアルパラメータ
		std::unordered_map<std::string, MaterialParameterValue> parameterOverrides{};

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

		// ライティングや影の適用を切り替えるフラグ
		MeshRenderFlags renderFlags = MeshRenderFlags::Default;

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
		Matrix4x4 BuildRenderLocalMatrix(const SubMeshMaterial& subMesh);

		// ランタイム更新
		void UpdateSubMeshRuntime(SubMeshMaterial& subMesh, const Matrix4x4& parentWorldMatrix);
		void UpdateRendererRuntime(MeshRendererComponent& renderer, const Matrix4x4& parentWorldMatrix);
	}

	ENGINE_REGISTER_COMPONENT(MeshRendererComponent, "MeshRenderer");
} // Engine
