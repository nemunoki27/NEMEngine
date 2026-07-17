#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	CanvasComponent struct
	//	スクリーンUIの基準解像度と入力設定を管理する
	//============================================================================
	enum class CanvasScaleMode :
		uint8_t {

		ConstantPixelSize,
		ScaleWithScreenSize,
	};

	enum class CanvasNavigationMode :
		uint8_t {

		Automatic,
		TransitionTable,
	};

	struct CanvasNavigationTable {

		static constexpr int32_t kMaxSize = 12;

		int32_t rows = 3;
		int32_t columns = 3;
		std::vector<UUID> cells = std::vector<UUID>(9);
	};

	struct CanvasComponent {

		bool enabled = true;

		// ゲーム解像度
		Vector2 referenceResolution = EngineContext::GetWindowSetting().gameSizeFloat;
		CanvasScaleMode scaleMode = CanvasScaleMode::ScaleWithScreenSize;
		float scaleFactor = 1.0f;
		float matchWidthOrHeight = 0.5f;

		int32_t sortingLayer = 0;
		int32_t order = 0;

		bool blockGameplayInput = true;
		bool inputInEditMode = false;
		bool blockInputAfterSubmit = false;
		bool wrapNavigation = true;
		CanvasNavigationMode navigationMode = CanvasNavigationMode::Automatic;
		CanvasNavigationTable navigationTable{};
		float repeatDelay = 0.35f;
		float repeatInterval = 0.12f;
		float stickThreshold = 0.5f;

		UUID firstSelectedLocalFileID{};

		// ランタイム入力状態
		UUID runtimeSelectedLocalFileID{};
		Vector2 runtimeRepeatDirection{};
		float runtimeRepeatElapsed = 0.0f;
		bool runtimeRepeatStarted = false;
		bool runtimeInputLocked = false;
	};

	// 遷移テーブルの行列数を変更する
	void ResizeCanvasNavigationTable(CanvasNavigationTable& table, int32_t rows, int32_t columns);

	void from_json(const nlohmann::json& in, CanvasComponent& component);
	void to_json(nlohmann::json& out, const CanvasComponent& component);

	ENGINE_REGISTER_COMPONENT(CanvasComponent, "Canvas");
} // Engine
