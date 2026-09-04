#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Components/Core/DynamicBuffer.h>
#include <Engine/Core/Runtime/Context/EngineContext.h>
#include <Engine/Core/Platform/Input/InputTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <span>
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

	enum class CanvasInputAction :
		uint8_t {

		Up,
		Down,
		Left,
		Right,
		Submit,
	};

	enum class CanvasInputDevice :
		uint8_t {

		Keyboard,
		Gamepad,
	};

	// Canvasの操作と入力コードを1本のBufferへ集約する
	struct CanvasInputBinding {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 16;
		static constexpr bool kSerializable = false;

		uint16_t code = 0;
		CanvasInputAction action = CanvasInputAction::Up;
		CanvasInputDevice device = CanvasInputDevice::Keyboard;
	};

	// 遷移テーブルのセル
	struct CanvasNavigationCell {

		static constexpr ComponentStorageKind kStorageKind =
			ComponentStorageKind::Buffer;
		static constexpr uint32_t kInternalBufferCapacity = 9;
		static constexpr bool kSerializable = false;

		UUID localFileID{};
	};

	// Systemだけが更新する入力状態
	struct CanvasRuntimeComponent {

		static constexpr bool kSerializable = false;

		UUID selectedLocalFileID{};
		Vector2 repeatDirection{};
		float repeatElapsed = 0.0f;
		bool repeatStarted = false;
		bool inputLocked = false;
	};

	// InspectorとJSON変換で使用する非ECSの編集データ
	struct CanvasNavigationTable {

		int32_t rows = 3;
		int32_t columns = 3;
		std::vector<UUID> cells = std::vector<UUID>(9);
	};

	enum class CanvasNavigationTableResult :
		uint8_t {

		Success,
		InvalidCanvas,
		InvalidSize,
		OutOfRange,
		InvalidTarget,
		AllocationFailed,
	};

	struct CanvasComponent {

		static constexpr bool kHasECSHooks = true;

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
		bool keyboardInputEnabled = true;
		bool gamepadInputEnabled = true;
		bool gamepadLeftStickEnabled = true;

		bool wrapNavigation = true;
		CanvasNavigationMode navigationMode = CanvasNavigationMode::Automatic;
		int32_t navigationRows = 3;
		int32_t navigationColumns = 3;
		float repeatDelay = 0.35f;
		float repeatInterval = 0.12f;
		float stickThreshold = 0.5f;

		UUID firstSelectedLocalFileID{};

		// Registryから呼ばれる入力BufferとRuntime状態のライフサイクル
		static void OnAdded(
			ECSWorld& world, const Entity& entity, CanvasComponent& component);
		static void OnRemoved(ECSWorld& world, const Entity& entity);
		static void InitializeStorage(
			ECSWorld& world, const Entity& entity, CanvasComponent& component);
		static void ReleaseStorage(
			ECSWorld& world, const Entity& entity, CanvasComponent& component);
		static void DeserializeECS(ECSWorld& world, const Entity& entity,
			const nlohmann::json& in, CanvasComponent& component);
		static void SerializeECS(const ECSWorld& world, const Entity& entity,
			const CanvasComponent& component, nlohmann::json& out);
	};

	// 遷移テーブルのセル数を安全に計算する
	bool TryGetCanvasNavigationCellCount(int32_t rows, int32_t columns, size_t& outCellCount);
	// 編集用遷移テーブルの行列数を変更する
	bool ResizeCanvasNavigationTable(CanvasNavigationTable& table, int32_t rows, int32_t columns);
	// 編集用遷移テーブルのセルへ重複なく設定する
	bool SetCanvasNavigationCell(CanvasNavigationTable& table, size_t index, UUID localFileID);
	// Canvas配下の遷移対象か判定する
	bool IsCanvasNavigationTarget(ECSWorld& world, const Entity& canvas, const Entity& target);
	// Canvasの遷移テーブルを変更する
	CanvasNavigationTableResult ResizeCanvasNavigationTable(
		ECSWorld& world, const Entity& canvas, int32_t rows, int32_t columns);
	// Canvasの遷移セルからEntityを取得する
	CanvasNavigationTableResult GetCanvasNavigationCell(
		ECSWorld& world, const Entity& canvas, int32_t row, int32_t column, Entity& outTarget);
	// Canvasの遷移セルへEntityを設定する
	CanvasNavigationTableResult SetCanvasNavigationCell(
		ECSWorld& world, const Entity& canvas, int32_t row, int32_t column, const Entity& target);

	void from_json(const nlohmann::json& in, CanvasComponent& component);
	void to_json(nlohmann::json& out, const CanvasComponent& component);
	std::span<CanvasInputBinding> GetCanvasInputBindings(
		ECSWorld& world, const Entity& entity);
	std::span<const CanvasInputBinding> GetCanvasInputBindings(
		const ECSWorld& world, const Entity& entity);
	void SetCanvasInputBindings(ECSWorld& world, const Entity& entity,
		std::span<const CanvasInputBinding> bindings);
	std::span<CanvasNavigationCell> GetCanvasNavigationCells(
		ECSWorld& world, const Entity& entity);
	std::span<const CanvasNavigationCell> GetCanvasNavigationCells(
		const ECSWorld& world, const Entity& entity);
	void SetCanvasNavigationCells(ECSWorld& world, const Entity& entity,
		std::span<const UUID> cells);
	void SerializeCanvas(const CanvasComponent& component,
		std::span<const CanvasInputBinding> bindings,
		std::span<const CanvasNavigationCell> cells, nlohmann::json& out);

} // Engine
