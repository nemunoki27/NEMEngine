#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <string>
#include <span>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	FillMeshRendererComponent struct
	//============================================================================
	// 面を構築するローカル座標
	struct FillMeshPosition {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 4;
		static constexpr bool kSerializable = false;

		Vector3 value = Vector3::AnyInit(0.0f);
	};

	// 三角形分割済みの頂点インデックス
	struct FillMeshTriangleIndex {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 6;
		static constexpr bool kSerializable = false;

		uint32_t value = 0;
	};

	// SystemとGPU抽出だけが使用する実行時状態
	struct FillMeshRuntimeStateComponent {

		static constexpr bool kSerializable = false;

		uint32_t geometryGeneration = 0;
	};

	// 座標を受け取って、メッシュ面を構築して描画
	struct FillMeshRendererComponent {

		static constexpr bool kHasECSHooks = true;

		// メッシュ構築フラグ、trueのフレームで構築
		bool buildMesh = false;

		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ
		MaterialParameterOverrides parameterOverrides{};

		// 色
		Color4 color = Color4::White();

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

		// Registryから呼ばれる点列とRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, FillMeshRendererComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, FillMeshRendererComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, FillMeshRendererComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, FillMeshRendererComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const FillMeshRendererComponent& component, nlohmann::json& out);
	};

	// json変換
	void from_json(const nlohmann::json& in, FillMeshRendererComponent& component);
	void to_json(nlohmann::json& out, const FillMeshRendererComponent& component);
	// Entityに付随する編集点列と三角形インデックス
	std::span<FillMeshPosition> GetFillMeshPositions(
		ECSWorld& world, const Entity& entity);
	std::span<const FillMeshPosition> GetFillMeshPositions(
		const ECSWorld& world, const Entity& entity);
	std::span<FillMeshTriangleIndex> GetFillMeshTriangleIndices(
		ECSWorld& world, const Entity& entity);
	std::span<const FillMeshTriangleIndex> GetFillMeshTriangleIndices(
		const ECSWorld& world, const Entity& entity);
	void SetFillMeshPositions(ECSWorld& world, const Entity& entity,
		std::span<const Vector3> positions);
	void SetFillMeshTriangleIndices(ECSWorld& world, const Entity& entity,
		std::span<const uint32_t> indices);
	// 点列を含む保存データへ変換する
	void SerializeFillMeshRenderer(const FillMeshRendererComponent& component,
		std::span<const FillMeshPosition> positions, nlohmann::json& out);

} // Engine
