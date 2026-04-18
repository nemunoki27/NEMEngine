#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/MathLib/Math.h>

namespace Engine {

	//============================================================================
	//	IRenderItemExtractor class
	//	描画アイテム抽出器のインターフェース
	//============================================================================
	class IRenderItemExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IRenderItemExtractor() = default;
		virtual ~IRenderItemExtractor() = default;

		// 描画アイテムの抽出
		virtual void Extract(ECSWorld& world, RenderSceneBatch& batch) = 0;
	};

	// アイテム抽出に関するユーティリティ関数
	namespace RenderItemExtract {

		// エンティティが描画可能か
		bool IsVisible(ECSWorld& world, const Entity& entity, bool visible);
		// ワールド行列を取得する。ワールド行列が存在しない場合は単位行列を返す
		Matrix4x4 GetWorldMatrix(ECSWorld& world, const Entity& entity);
		// シーンオブジェクトコンポーネントを取得する
		const SceneObjectComponent* GetSceneObject(ECSWorld& world, const Entity& entity);

		// 描画アイテムの共通フィールドを埋める
		template <typename T>
		inline void FillCommonFields(RenderItem& item, ECSWorld& world,
			const Entity& entity, const T& renderer, const Matrix4x4& worldMatrix) {

			const SceneObjectComponent* sceneObject = GetSceneObject(world, entity);

			item.entity = entity;
			item.world = &world;
			item.sceneInstanceID = sceneObject ? sceneObject->sceneInstanceID : UUID{};
			item.renderPhase = renderer.queue;
			item.visibilityLayerMask = sceneObject ? sceneObject->visibilityLayerMask : 0xFFFFFFFFu;
			item.sortingLayer = renderer.layer;
			item.sortingOrder = renderer.order;
			item.blendMode = renderer.blendMode;
			item.worldMatrix = worldMatrix;
		}
	}
} // Engine