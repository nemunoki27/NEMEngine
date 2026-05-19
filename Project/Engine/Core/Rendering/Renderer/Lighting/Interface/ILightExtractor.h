#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Rendering/Renderer/Lighting/FrameLightBatch.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine {

	//============================================================================
	//	ILightExtractor class
	//	ライト抽出器のインターフェース
	//============================================================================
	class ILightExtractor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ILightExtractor() = default;
		virtual ~ILightExtractor() = default;

		// ライト抽出
		virtual void Extract(ECSWorld& world, FrameLightBatch& batch) = 0;
	};

	// ライト抽出に関するユーティリティ
	namespace LightExtract {

		// エンティティがライト抽出可能か
		bool IsVisible(ECSWorld& world, const Entity& entity, bool enabled);

		// ワールド行列取得
		Matrix4x4 GetWorldMatrix(ECSWorld& world, const Entity& entity);
		// シーンオブジェクトコンポーネントの取得
		const SceneObjectComponent* GetSceneObject(ECSWorld& world, const Entity& entity);

		// 位置の取得
		Vector3 GetWorldPos(ECSWorld& world, const Entity& entity);
		// 方向の取得
		Vector3 GetWorldDirection(const Vector3& localDirection, const Matrix4x4& worldMatrix);

		// 共通フィールド設定
		template<class TLightComponent>
		inline void FillCommonFields(LightItemCommon& common, ECSWorld& world,
			const Entity& entity, const TLightComponent& component) {

			const SceneObjectComponent* sceneObject = GetSceneObject(world, entity);

			common.entity = entity;
			common.world = &world;
			common.sceneInstanceID = sceneObject ? sceneObject->sceneInstanceID : UUID{};

			common.affectLayerMask = component.affectLayerMask;
			if (sceneObject) {

				common.affectLayerMask &= sceneObject->visibilityLayerMask;
			}
			common.cameraDomain = RenderCameraDomain::Perspective;
		}
	}
} // Engine