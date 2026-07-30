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
#include <span>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	MeshRendererComponent struct
	//============================================================================
	// ライティングや影の適用をビット単位で切り替えるフラグ
	enum class MeshRenderFlags : uint32_t {

		None = 0,
		// ライティングを行うか
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
		// 全フラグ有効
		Default = Lighting | CastShadow | ReceiveShadow | ReceiveIBL | CastReflection | ReceiveReflection,
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

	// MeshRenderFlagsのjson入出力、名前付きbool群で保存しMesh/Primitiveで共用する
	void ReadMeshRenderFlags(const nlohmann::json& in, MeshRenderFlags& flags);
	void WriteMeshRenderFlags(nlohmann::json& out, MeshRenderFlags flags);

	struct SubMeshMaterial {

		static constexpr ComponentStorageKind kStorageKind = ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 0;
		static constexpr bool kSerializable = false;
		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;

		// 表示用の名前
		std::string name;

		// 元のサブメッシュインデックス
		UUID stableID{};
		// 元メッシュ内でのインデックス
		uint32_t sourceSubMeshIndex = 0;

		// シェーダーごとのマテリアルパラメータ
		MaterialParameterOverrides parameterOverrides{};

		// UV
		Vector2 uvPos = Vector2::AnyInit(0.0f);
		float uvRotation = 0.0f;
		Vector2 uvScale = Vector2::AnyInit(1.0f);

		// ローカル変換(Entityが親)
		Vector3 localPos = Vector3::AnyInit(0.0f);
		Vector3 localRotation = Vector3::AnyInit(0.0f);
		Vector3 localScale = Vector3::AnyInit(1.0f);

		// 頂点座標から計算したピボット
		Vector3 sourcePivot = Vector3::AnyInit(0.0f);
	};

	// メッシュ描画
	struct MeshRendererComponent {

		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::Render;

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

		// Registryから呼ばれるワールド依存Storageフック
		static void OnAdded(ECSWorld& world, const Entity& entity, MeshRendererComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(ECSWorld& world, const Entity& entity, MeshRendererComponent& component);
		static void ReleaseStorage(ECSWorld& world, const Entity& entity, MeshRendererComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity, const nlohmann::json& in, MeshRendererComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity, const MeshRendererComponent& component, nlohmann::json& out);
	};

	// json変換
	void from_json(const nlohmann::json& in, SubMeshMaterial& subMeshMaterial);
	void to_json(nlohmann::json& out, const SubMeshMaterial& subMeshMaterial);
	void from_json(const nlohmann::json& in, MeshRendererComponent& component);
	void to_json(nlohmann::json& out, const MeshRendererComponent& component);
	// Entityに付随するサブメッシュ編集データ
	std::span<SubMeshMaterial> GetMeshSubMeshes(ECSWorld& world, const Entity& entity);
	std::span<const SubMeshMaterial> GetMeshSubMeshes(const ECSWorld& world, const Entity& entity);
	void SetMeshSubMeshes(ECSWorld& world, const Entity& entity, std::span<const SubMeshMaterial> subMeshes);
	// サブメッシュを含む保存データへ変換する
	void SerializeMeshRenderer(const MeshRendererComponent& component, std::span<const SubMeshMaterial> subMeshes, nlohmann::json& out);

	// helpers
	namespace MeshSubMeshRuntime {

		// 行列の構築
		Matrix4x4 BuildUVMatrix(const SubMeshMaterial& subMesh);
		Matrix4x4 BuildLocalMatrix(const SubMeshMaterial& subMesh);
		Matrix4x4 BuildRenderLocalMatrix(const SubMeshMaterial& subMesh);
	}
} // Engine