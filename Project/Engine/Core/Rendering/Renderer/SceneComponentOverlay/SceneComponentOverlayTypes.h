#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>

// c++
#include <cstdint>
#include <vector>

namespace Engine {

	//============================================================================
	//	SceneComponentOverlay structures
	//============================================================================

	enum class SceneComponentOverlayKind :
		uint8_t {

		// 画面座標に射影して描く2Dアイコン
		LightIcon,
		// PerspectiveCameraComponent用の2Dアイコン
		CameraIcon,
	};

	enum class SceneComponentOverlayComponentKind :
		uint8_t {

		// 対応コンポーネント種別でRegistryの検索キーとして使う
		DirectionalLight,
		PointLight,
		SpotLight,
		PerspectiveCamera,
	};

	// SceneView Overlay全体の調整値でマジックナンバーを各処理へ散らさないための設定
	struct SceneComponentOverlaySettings {

		// ライトアイコンの距離フェード用設定
		float lightIconNearDistance = 2.0f;
		float lightIconFarDistance = 60.0f;
		float lightIconHideDistance = 180.0f;
		float lightIconMaxPixelSize = 200.0f;
		float lightIconMinPixelSize = 0.0f;
		// 形を判別できず黒点に見える小さなアイコンは描画しない
		float lightIconCullPixelSize = 48.0f;
		// 無効コンポーネントを半透明表示するためのアルファ
		float disabledAlpha = 0.42f;
		// 同一点に重なるOverlayを決定的に少しずらす量
		float overlapPixelOffset = 5.0f;
	};

	// Collectorが作り、RendererとPickerが共有する1表示要素分のデータ
	struct SceneComponentOverlayItem {

		// 表示種別と対象コンポーネント種別
		SceneComponentOverlayKind kind = SceneComponentOverlayKind::LightIcon;
		SceneComponentOverlayComponentKind componentKind = SceneComponentOverlayComponentKind::PointLight;
		// 選択対象Entityと表示に使うBuiltinアセット
		Entity entity = Entity::Null();
		AssetID asset{};

		// 3D表示や距離計算に使うワールド情報
		Vector3 worldPosition = Vector3::AnyInit(0.0f);

		// 2Dアイコン描画と矩形ピックに使うスクリーン情報
		Vector2 screenCenter = Vector2::AnyInit(0.0f);
		Vector2 rectMin = Vector2::AnyInit(0.0f);
		Vector2 rectMax = Vector2::AnyInit(0.0f);
		float spriteRotationRadians = 0.0f;
		float viewDepth = 0.0f;
		float distanceToCamera = 0.0f;

		// 表示色と安定ソート用情報
		Color4 color = Color4::White();
		bool enabled = true;
		uint32_t stableOrder = 0;
	};

	// フレーム内で扱うOverlayアイテム配列
	using SceneComponentOverlayItemList = std::vector<SceneComponentOverlayItem>;
}
