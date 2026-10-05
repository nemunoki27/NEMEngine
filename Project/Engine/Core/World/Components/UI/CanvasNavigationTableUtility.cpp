#include "CanvasComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace {

	// 行列数の変更前と重なるセルを同じ位置へ移す
	template <typename Cell, typename ReadID>
	void CopyNavigationCells(std::span<const Cell> source, int32_t sourceRows, int32_t sourceColumns, int32_t targetRows,
		int32_t targetColumns, std::span<Engine::UUID> target, ReadID readID) {

		const int32_t rows = (std::min)(sourceRows, targetRows);
		const int32_t columns = (std::min)(sourceColumns, targetColumns);
		for (int32_t row = 0; row < rows; ++row) {
			for (int32_t column = 0; column < columns; ++column) {
				const size_t from = static_cast<size_t>(row) * static_cast<size_t>(sourceColumns) + static_cast<size_t>(column);
				const size_t to = static_cast<size_t>(row) * static_cast<size_t>(targetColumns) + static_cast<size_t>(column);
				if (from < source.size()) {
					target[to] = readID(source[from]);
				}
			}
		}
	}
}

bool Engine::TryGetCanvasNavigationCellCount(int32_t rows, int32_t columns, size_t& outCellCount) {

	// Bufferの格納上限を超える行列数を除外する
	outCellCount = 0;
	if (rows <= 0 || columns <= 0) {
		return false;
	}

	const size_t rowCount = static_cast<size_t>(rows);
	const size_t columnCount = static_cast<size_t>(columns);
	if (rowCount > static_cast<size_t>((std::numeric_limits<uint32_t>::max)()) / columnCount) {
		return false;
	}
	outCellCount = rowCount * columnCount;
	return true;
}

bool Engine::ResizeCanvasNavigationTable(CanvasNavigationTable& table, int32_t rows, int32_t columns) {

	// 新しいセルを確保してから編集データを差し替える
	size_t cellCount = 0;
	if (!TryGetCanvasNavigationCellCount(rows, columns, cellCount)) {
		return false;
	}
	if (table.rows == rows && table.columns == columns && table.cells.size() == cellCount) {
		return true;
	}

	std::vector<UUID> resized;
	try {
		resized.resize(cellCount);
	} catch (const std::bad_alloc&) {
		return false;
	} catch (const std::length_error&) {
		return false;
	}
	// 編集用の参照IDを変更後の行列へ移す
	CopyNavigationCells<UUID>(table.cells, table.rows, table.columns, rows, columns, resized, [](UUID value) { return value; });

	table.rows = rows;
	table.columns = columns;
	table.cells = std::move(resized);
	return true;
}

bool Engine::SetCanvasNavigationCell(CanvasNavigationTable& table, size_t index, UUID localFileID) {

	// 同じ対象の重複を解除してセルを更新する
	if (table.cells.size() <= index) {
		return false;
	}
	if (localFileID) {
		for (UUID& cell : table.cells) {
			if (cell == localFileID) {
				cell = {};
			}
		}
	}
	table.cells[index] = localFileID;
	return true;
}

bool Engine::IsCanvasNavigationTarget(ECSWorld& world, const Entity& canvas, const Entity& target) {

	// 同じCanvas配下の選択可能な対象だけを認める
	if (!world.IsAlive(canvas) || !world.IsAlive(target) || canvas == target || !world.HasComponent<CanvasComponent>(canvas) ||
		!world.HasComponent<UISelectableComponent>(target)) {
		return false;
	}

	Entity current = target;
	while (world.IsAlive(current)) {

		if (current == canvas) {
			return true;
		}
		if (current != target && world.HasComponent<CanvasComponent>(current)) {
			return false;
		}
		const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(current);
		current = hierarchy ? hierarchy->parent : Entity::Null();
	}
	return false;
}

Engine::CanvasNavigationTableResult Engine::ResizeCanvasNavigationTable(
	ECSWorld& world, const Entity& canvas, int32_t rows, int32_t columns) {

	// 行列とBufferを確定するまで元の設定を保つ
	if (!world.IsAlive(canvas) || !world.HasComponent<CanvasComponent>(canvas)) {
		return CanvasNavigationTableResult::InvalidCanvas;
	}

	size_t cellCount = 0;
	if (!TryGetCanvasNavigationCellCount(rows, columns, cellCount)) {
		return CanvasNavigationTableResult::InvalidSize;
	}

	const auto& component = world.GetComponent<CanvasComponent>(canvas);
	const std::span<const CanvasNavigationCell> current = GetCanvasNavigationCells(world, canvas);
	std::vector<UUID> resized;
	try {
		resized.resize(cellCount);
	} catch (const std::bad_alloc&) {
		return CanvasNavigationTableResult::AllocationFailed;
	} catch (const std::length_error&) {
		return CanvasNavigationTableResult::AllocationFailed;
	}

	// ECSのセルを編集用と同じ行列規則で移す
	CopyNavigationCells<CanvasNavigationCell>(current, component.navigationRows, component.navigationColumns, rows, columns,
		resized, [](const CanvasNavigationCell& cell) { return cell.localFileID; });

	try {
		SetCanvasNavigationCells(world, canvas, resized);
	} catch (const std::bad_alloc&) {
		return CanvasNavigationTableResult::AllocationFailed;
	} catch (const std::length_error&) {
		return CanvasNavigationTableResult::AllocationFailed;
	}
	auto& resizedComponent = world.GetComponent<CanvasComponent>(canvas);
	resizedComponent.navigationRows = rows;
	resizedComponent.navigationColumns = columns;
	world.MarkComponentModified<CanvasComponent>(canvas);
	return CanvasNavigationTableResult::Success;
}

Engine::CanvasNavigationTableResult Engine::GetCanvasNavigationCell(
	ECSWorld& world, const Entity& canvas, int32_t row, int32_t column, Entity& outTarget) {

	// Canvasと同じSceneから参照先を解決する
	outTarget = Entity::Null();
	if (!world.IsAlive(canvas) || !world.HasComponent<CanvasComponent>(canvas)) {
		return CanvasNavigationTableResult::InvalidCanvas;
	}

	const auto& component = world.GetComponent<CanvasComponent>(canvas);
	if (row < 0 || component.navigationRows <= row || column < 0 || component.navigationColumns <= column) {
		return CanvasNavigationTableResult::OutOfRange;
	}
	const size_t index =
		static_cast<size_t>(row) * static_cast<size_t>(component.navigationColumns) + static_cast<size_t>(column);
	const std::span<const CanvasNavigationCell> cells = GetCanvasNavigationCells(world, canvas);
	if (cells.size() <= index || !cells[index].localFileID) {
		return CanvasNavigationTableResult::Success;
	}

	const auto* sceneObject = world.TryGetComponent<SceneObjectComponent>(canvas);
	outTarget = SceneObjectUtility::FindByLocalFileID(
		world, sceneObject ? sceneObject->sceneInstanceID : UUID{}, cells[index].localFileID);
	if (!IsCanvasNavigationTarget(world, canvas, outTarget)) {
		outTarget = Entity::Null();
	}
	return CanvasNavigationTableResult::Success;
}

Engine::CanvasNavigationTableResult Engine::SetCanvasNavigationCell(
	ECSWorld& world, const Entity& canvas, int32_t row, int32_t column, const Entity& target) {

	// 範囲と所属を確認して選択遷移先を差し替える
	if (!world.IsAlive(canvas) || !world.HasComponent<CanvasComponent>(canvas)) {
		return CanvasNavigationTableResult::InvalidCanvas;
	}
	const auto& component = world.GetComponent<CanvasComponent>(canvas);
	if (row < 0 || component.navigationRows <= row || column < 0 || component.navigationColumns <= column) {
		return CanvasNavigationTableResult::OutOfRange;
	}
	if (target.IsValid() && !IsCanvasNavigationTarget(world, canvas, target)) {
		return CanvasNavigationTableResult::InvalidTarget;
	}

	std::span<CanvasNavigationCell> cells = GetCanvasNavigationCells(world, canvas);
	const size_t index =
		static_cast<size_t>(row) * static_cast<size_t>(component.navigationColumns) + static_cast<size_t>(column);
	if (cells.size() <= index) {
		return CanvasNavigationTableResult::OutOfRange;
	}

	UUID localFileID{};
	if (target.IsValid()) {
		const auto* sceneObject = world.TryGetComponent<SceneObjectComponent>(target);
		if (!sceneObject || !sceneObject->localFileID) {
			return CanvasNavigationTableResult::InvalidTarget;
		}
		localFileID = sceneObject->localFileID;
		for (CanvasNavigationCell& cell : cells) {
			if (cell.localFileID == localFileID) {
				cell.localFileID = {};
			}
		}
	}
	cells[index].localFileID = localFileID;
	world.MarkComponentModified<CanvasNavigationCell>(canvas);
	return CanvasNavigationTableResult::Success;
}
