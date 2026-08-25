#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <span>
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	LineRendererComponent struct
	//============================================================================
	// ライン描画、A→B→C…と連続したポリラインを1コンポーネントで持つ
	struct LineRendererComponent {

		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels =
			ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels =
			ComponentChangeChannel::Render;

		// マテリアル
		AssetID material{};
		// エンティティごとのマテリアルパラメータ上書き
		MaterialInstanceParameters materialInstance{};

		// 始点と終点をつないで閉じるか
		bool loop = false;

		// 2D描画か、trueなら正射影カメラでxyのみ使う
		bool is2D = false;
		// 点をワールド絶対座標として扱うか、falseなら親Transformに追従する
		bool useWorldSpace = true;
		// 親エンティティのlocalFileID、0なら自身のTransformを親にする。useWorldSpace=falseで有効
		UUID parentLocalFileID{};
		// 親のスケールを無視するか
		bool ignoreParentScale = false;
		// 親の回転を無視するか
		bool ignoreParentRotation = false;

		// 描画レイヤー
		int32_t layer = 0;
		// 描画レイヤー内の中での順序
		int32_t order = 0;
		// 表示フラグ
		bool visible = true;
		// RenderFeatureとLightが参照する描画レイヤー
		uint32_t renderingLayerMask = 1u;

		// ブレンドモード
		BlendMode blendMode = BlendMode::Normal;
		// 描画キュー
		RenderPhase queue = RenderPhase::Transparent;

		// Registryから呼ばれる点列Bufferのライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, LineRendererComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, LineRendererComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, LineRendererComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, LineRendererComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const LineRendererComponent& component, nlohmann::json& out);
	};

	// json変換
	void from_json(const nlohmann::json& in, LineRendererComponent& component);
	void to_json(nlohmann::json& out, const LineRendererComponent& component);
	// Entityに付随するライン点列
	std::span<LinePoint> GetLinePoints(ECSWorld& world, const Entity& entity);
	std::span<const LinePoint> GetLinePoints(
		const ECSWorld& world, const Entity& entity);
	void SetLinePoints(ECSWorld& world, const Entity& entity,
		std::span<const LinePoint> points);
	// 点列を含む保存データへ変換する
	void SerializeLineRenderer(const LineRendererComponent& component,
		std::span<const LinePoint> points, nlohmann::json& out);

} // Engine
