#include "ViewLightCollector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scene/Instance/SceneInstanceManager.h>

//============================================================================
//	ViewLightCollector classMethods
//============================================================================

namespace {

	// ライトが有効か
	template <typename T>
	bool IsLightVisibleToCamera(const T& item, const Engine::SceneInstance* sceneInstance,
		const Engine::ResolvedCameraView& camera) {

		// シーンインスタンスが同じ、有効な場合のみ
		if (sceneInstance && item.common.sceneInstanceID != sceneInstance->instanceID) {
			return false;
		}
		// カメラドメインが一致しないライトは除外
		if (item.common.cameraDomain != Engine::RenderCameraDomain::Perspective) {
			return false;
		}
		// ライトのレイヤーマスクとカメラのカリングマスクが共通ビットを持たない場合は除外
		if ((item.common.affectLayerMask & camera.cullingMask) == 0) {
			return false;
		}
		return true;
	}
	// 有効なライトを追加
	template <typename T>
	void AppendVisibleLights(const std::vector<T>& source, const Engine::SceneInstance* sceneInstance,
		const Engine::ResolvedCameraView& camera, std::vector<const T*>& out) {

		out.reserve(out.size() + source.size());
		for (const T& item : source) {
			if (!IsLightVisibleToCamera(item, sceneInstance, camera)) {
				continue;
			}
			out.emplace_back(&item);
		}
	}
}

void Engine::ViewLightCollector::CollectForView(const FrameLightBatch& batch, const SceneInstance* sceneInstance,
	const ResolvedRenderView& view, PerViewLightSet& outSet) {

	// 出力セットを初期化
	outSet.Clear();
	outSet.view = &view;
	if (!view.valid) {
		return;
	}

	// カメラを取得
	const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		return;
	}

	// カメラをセット
	outSet.camera = camera;
	// 有効なライトを追加
	AppendVisibleLights(batch.GetDirectionalLights(), sceneInstance, *camera, outSet.directionalLights);
	AppendVisibleLights(batch.GetPointLights(), sceneInstance, *camera, outSet.pointLights);
	AppendVisibleLights(batch.GetSpotLights(), sceneInstance, *camera, outSet.spotLights);
}