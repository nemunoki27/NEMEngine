#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Render/View/RenderFrameTypes.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/MathLib/Vector2.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	RenderViewResolver class
	//	描画ビューの情報を確定させるクラス
	//============================================================================
	class RenderViewResolver {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderViewResolver() = default;
		~RenderViewResolver() = default;

		// リクエストから描画ビューを確定させる
		static ResolvedRenderView Resolve(const RenderViewRequest& request, ECSWorld& world);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// ワールド内のカメラから描画ビューを確定させる
		static ResolvedRenderView ResolveWorldCameraView(RenderViewKind kind, ECSWorld& world,
			uint32_t width, uint32_t height, UUID preferredOrthographicCameraUUID, UUID preferredPerspectiveCameraUUID);
		// 手動で指定されたカメラから描画ビューを確定させる
		static ResolvedRenderView BuildFromManualCamera(RenderViewKind kind,
			const ManualRenderCameraState& state, uint32_t width, uint32_t height);

		//-------- orthographic --------------------------------------------------

		static ResolvedCameraView ResolveBestOrthographicCamera(ECSWorld& world, uint32_t width, uint32_t height);
		static ResolvedCameraView BuildFromOrthographicCamera(const Entity& entity,
			const TransformComponent& transform, OrthographicCameraComponent& camera);
		static ResolvedCameraView BuildManualOrthographic(const ManualRenderCameraState& state, uint32_t width, uint32_t height);
		static ResolvedCameraView ResolvePreferredOrthographicCamera(ECSWorld& world, UUID preferredCameraUUID);

		//-------- perspective ---------------------------------------------------

		static ResolvedCameraView ResolveBestPerspectiveCamera(ECSWorld& world, uint32_t width, uint32_t height);
		static ResolvedCameraView BuildFromPerspectiveCamera(const Entity& entity,
			const TransformComponent& transform, PerspectiveCameraComponent& camera);
		static ResolvedCameraView BuildManualPerspective(const ManualRenderCameraState& state, uint32_t width, uint32_t height);
		static ResolvedCameraView ResolvePreferredPerspectiveCamera(ECSWorld& world, UUID preferredCameraUUID);
	};
} // Engine