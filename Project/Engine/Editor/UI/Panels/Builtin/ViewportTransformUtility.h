#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

namespace Engine::ViewportTransformUtility {

	// 編集座標を解決する
	Engine::Vector3 RotateVectorByQuaternion(const Engine::Quaternion& q, const Engine::Vector3& v);
	// 編集座標を解決する
	bool Prefers2DGizmo(const Engine::EditorPanelContext& context, Engine::ECSWorld& world, const Engine::Entity& entity);
	// 編集座標を解決する
	const Engine::ResolvedCameraView* SelectSceneGizmoCamera(const Engine::ResolvedRenderView& view, bool prefer2DTarget);
	// 編集座標を解決する
	Engine::Matrix4x4 GetEntityParentWorldMatrix(Engine::ECSWorld& world, const Engine::Entity& entity);
	// 編集座標を解決する
	float SnapValueToGrid(float value, float grid);
	// 編集座標を解決する
	const Engine::GridSnapAxis* SelectSnapAxis(const Engine::EntitySnapSettings& settings,
	Engine::SceneViewManipulatorMode mode, bool use2D);
	// 編集座標を解決する
	void ApplyAbsoluteSnap(Engine::TransformComponent& transform, Engine::SceneViewManipulatorMode mode, float grid);
}
