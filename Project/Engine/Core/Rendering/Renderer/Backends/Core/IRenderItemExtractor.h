#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

namespace Engine {

	//============================================================================
	//	IRenderItemExtractor class
	//	描画アイテム抽出器のインターフェース
	//============================================================================
	class IRenderItemExtractor {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		IRenderItemExtractor() = default;
		virtual ~IRenderItemExtractor() = default;

		// 描画アイテムの抽出
		virtual void Extract(ECSWorld& world, RenderSceneBatch& batch) = 0;
		// World外の描画データの更新を通知する
		virtual uint64_t GetContentRevision() const;
	};

	// アイテム抽出に関するユーティリティ関数
	namespace RenderItemExtract {

		// エンティティが描画可能か
		bool IsVisible(ECSWorld& world, const Entity& entity, bool visible);
		// ワールド行列を取得し存在しない場合は単位行列を返す
		Matrix4x4 GetWorldMatrix(ECSWorld& world, const Entity& entity);
		// シーンオブジェクトコンポーネントを取得する
		const SceneObjectComponent* GetSceneObject(ECSWorld& world, const Entity& entity);

		// SceneとRendererの可視レイヤーを合わせる
		uint32_t GetVisibilityLayerMask(ECSWorld& world, const Entity& entity, uint32_t renderingLayerMask);

		// 描画アイテムの共通フィールドを埋める
		template <typename T>
		inline void FillCommonFields(RenderItem& item, ECSWorld& world,
			const Entity& entity, const T& renderer, const Matrix4x4& worldMatrix) {

			item.entity = entity;
			item.world = &world;
			item.sceneInstanceID = SceneObjectUtility::GetSceneInstanceID(world, entity);
			item.renderPhase = renderer.queue;
			if constexpr (requires { renderer.renderingLayerMask; }) {
				item.renderingLayerMask = renderer.renderingLayerMask &
					kRenderingLayerMaskBits;
			}
			item.visibilityLayerMask = GetVisibilityLayerMask(world, entity, item.renderingLayerMask);
			item.sortingLayer = renderer.layer;
			item.sortingOrder = renderer.order;
			item.blendMode = renderer.blendMode;
			item.worldMatrix = worldMatrix;
			item.sortPosition = worldMatrix.GetTranslationValue();
		}
	}
} // Engine
