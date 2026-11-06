#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <cstdint>
#include <span>
#include <vector>

namespace Engine {

	//============================================================================
	//	RenderViewResolver class
	//	描画ビューの情報を確定させるクラス
	//============================================================================
	class RenderViewResolver {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RenderViewResolver() = default;
		~RenderViewResolver() = default;

		// リクエストから描画ビューを確定させる
		static ResolvedRenderView Resolve(const RenderViewRequest& request, ECSWorld& world);
		// Game Viewへ出力する各CameraのViewを確定する
		static std::vector<ResolvedRenderView> ResolveGameCameraViews(
			const RenderViewRequest& request, ECSWorld& world);
		// RenderTextureはAssetの指定サイズを画面座標と描画の両方に使う
		static void ResolveRenderTextureOutput(ResolvedRenderView& view, ECSWorld& world,
			uint32_t width, uint32_t height);
		// 出力画素を最後に描いたCameraと、そのCamera内の画素座標を取得する
		static const ResolvedRenderView* FindOutputViewAtPixel(std::span<const ResolvedRenderView> views,
			const Vector2& inputPixel, Vector2& viewPixel);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// ワールド内のカメラから描画ビューを確定させる
		static ResolvedRenderView ResolveWorldCameraView(RenderViewKind kind, ECSWorld& world,
			uint32_t width, uint32_t height, UUID preferredOrthographicCameraUUID, UUID preferredPerspectiveCameraUUID);
		// 手動で指定されたカメラから描画ビューを確定させる
		static ResolvedRenderView BuildFromManualCamera(RenderViewKind kind,
			const ManualRenderCameraState& state, uint32_t width, uint32_t height);

	};
} // Engine
