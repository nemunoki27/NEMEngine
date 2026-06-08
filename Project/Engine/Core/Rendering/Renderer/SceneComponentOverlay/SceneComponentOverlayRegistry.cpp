#include "SceneComponentOverlayRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

//============================================================================
//	SceneComponentOverlayRegistry classMethods
//============================================================================
Engine::SceneComponentOverlayRegistry::SceneComponentOverlayRegistry() {

	// ここへ追加すると、Collector側で対応コンポーネントを拾うだけでOverlay表示へ参加できる
	registrations_.push_back({ SceneComponentOverlayComponentKind::DirectionalLight,
		SceneComponentOverlayKind::LightIcon, BuiltinAssets::EditorTextures::DirectionalLightIcon });
	registrations_.push_back({ SceneComponentOverlayComponentKind::PointLight,
		SceneComponentOverlayKind::LightIcon, BuiltinAssets::EditorTextures::PointLightIcon });
	registrations_.push_back({ SceneComponentOverlayComponentKind::SpotLight,
		SceneComponentOverlayKind::LightIcon, BuiltinAssets::EditorTextures::SpotLightIcon });
	registrations_.push_back({ SceneComponentOverlayComponentKind::PerspectiveCamera,
		SceneComponentOverlayKind::CameraIcon, BuiltinAssets::EditorTextures::PerspectiveCameraIcon });
}

const Engine::SceneComponentOverlayRegistry::Registration*
Engine::SceneComponentOverlayRegistry::Find(SceneComponentOverlayComponentKind kind) const {

	for (const Registration& registration : registrations_) {
		if (registration.componentKind == kind) {
			return &registration;
		}
	}
	return nullptr;
}
