#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <array>
#include <cstdint>

namespace Engine {

	// front
	class SceneInstanceManager;
	class ECSWorld;
	class AssetDatabase;
	struct SceneHeader;
	struct SystemContext;

	//============================================================================
	//	RenderFrame structures
	//============================================================================

	// 描画ビューがどのカメラソースを使うか
	enum class RenderViewSourceKind :
		uint8_t {

		WorldCamera,
		ManualCamera,
	};

	// 1つの描画ビューに対する要求
	struct RenderViewRequest {

		// ビューの種類
		RenderViewKind kind = RenderViewKind::Game;

		// 有効かどうか
		bool enabled = true;

		// 出力サイズ
		uint32_t width = 0;
		uint32_t height = 0;

		// どのカメラソースからビューを解決するか
		RenderViewSourceKind sourceKind = RenderViewSourceKind::WorldCamera;

		// WorldCamera使用時に優先するカメラ
		UUID preferredOrthographicCameraUUID{};
		UUID preferredPerspectiveCameraUUID{};
		// ManualCamera使用時の状態
		ManualRenderCameraState manualCamera{};
	};

	// 1フレーム分の描画要求
	struct RenderFrameRequest {

		// シーンとワールド
		const SceneInstanceManager* sceneInstances = nullptr;
		const SceneHeader* header = nullptr;
		UUID activeSceneInstanceID{};
		ECSWorld* world = nullptr;
		const SystemContext* systemContext = nullptr;
		AssetDatabase* assetDatabase = nullptr;

		// 描画ビューの要求種類
		std::array<RenderViewRequest, 2> views = {
			RenderViewRequest{ RenderViewKind::Game,  true, 0, 0, RenderViewSourceKind::WorldCamera, {} },
			RenderViewRequest{ RenderViewKind::Scene, true, 0, 0, RenderViewSourceKind::ManualCamera, {} }
		};

		// ビューを種類から検索
		const RenderViewRequest* FindView(RenderViewKind kind) const;
		RenderViewRequest* FindView(RenderViewKind kind);
	};
} // Engine