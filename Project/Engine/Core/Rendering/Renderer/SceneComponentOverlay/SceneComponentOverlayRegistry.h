#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/SceneComponentOverlay/SceneComponentOverlayTypes.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	//============================================================================
	//	SceneComponentOverlayRegistry class
	//	描画を持たないSceneView用コンポーネント表示の登録表
	//============================================================================
	class SceneComponentOverlayRegistry {
	public:
		// 1コンポーネント種別に対するOverlay種別とBuiltinアセットの対応
		struct Registration {

			SceneComponentOverlayComponentKind componentKind = SceneComponentOverlayComponentKind::PointLight;
			SceneComponentOverlayKind overlayKind = SceneComponentOverlayKind::LightIcon;
			AssetID asset{};
		};

		SceneComponentOverlayRegistry();
		~SceneComponentOverlayRegistry() = default;

		// 登録済み定義の列挙で将来の拡張やデバッグ表示で使う
		std::span<const Registration> GetRegistrations() const { return registrations_; }
		// Collectorが対象コンポーネントから表示アセットを引くための検索
		const Registration* Find(SceneComponentOverlayComponentKind kind) const;
	private:
		// 登録順はstableOrderの元にもなるため、初期化順を固定する
		std::vector<Registration> registrations_{};
	};
}
