#pragma once

#include <Engine/Core/Foundation/Geometry/LogicPlacement.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

#include <memory>
#include <imgui.h>

namespace Engine {

	struct EditorPanelContext;
	class ECSWorldLifetime;

	// シーンへ保存しない配置候補の表示状態
	struct LogicPlacementPreview {

		std::shared_ptr<const LogicPlacementResult> layout;
		std::weak_ptr<const ECSWorldLifetime> lifetime;
		Matrix4x4 transform = Matrix4x4::Identity();
		UUID scene{};
		UUID owner{};
		int32_t requestedFrame = -100;
		bool is2D = false;
	};

	// ツールとSplineの候補をSceneViewへ渡す
	void SubmitLogicPlacementPreview(const EditorPanelContext& context, std::shared_ptr<const LogicPlacementResult> layout,
		const Matrix4x4& transform, bool is2D, UUID splineOwner = {});
	// SceneView画像の範囲内へ配置線と球を描く
	void DrawLogicPlacementPreview(const EditorPanelContext& context, ImVec2 imagePosition, ImVec2 imageSize);
}
