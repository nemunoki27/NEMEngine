#include "TestContracts.h"
#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Scripting/Managed/ScriptExecutionOrderSettings.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestScriptProfiler() {
		using namespace Engine;
		auto& profiler = ScriptProfiler::GetInstance();
		profiler.ResetOwners();
		ManagedScriptInstanceHandle first{ 7, 1 }, second{ 8, 1 };
		ManagedNativeEntity entity{ { 1, 1 }, 2, 0 };
		profiler.Register({ ScriptProfiler::OwnerID(first), entity, 11, "type-a", "Example.Rain" });
		profiler.Register({ ScriptProfiler::OwnerID(second), entity, 12, "type-a", "Example.Rain" });
		profiler.Configure(false, {}, 0);
		bool passed = profiler.BeginCallback(first, "Update") == 0 && profiler.Rows().empty();
		profiler.Configure(true, "Example.Rain", ScriptProfiler::OwnerID(first));
		profiler.BeginFrame();
		passed &= profiler.BeginDetail(entity, 12, "対象外") == 0;
		const uint64_t callback = profiler.BeginCallback(second, "LateUpdate");
		const uint64_t outer = profiler.BeginDetail(entity, 11, "更新");
		const uint64_t inner = profiler.BeginDetail(entity, 11, "移動");
		profiler.End(inner, true);
		profiler.End(outer, true);
		profiler.End(callback, false);
		try {
			ScriptProfileScope scope(first, "例外");
			throw 1;
		} catch (int) {
		}
		profiler.EndFrame();
		bool foundChild = false, foundException = false;
		for (const auto& row : profiler.Rows()) {
			const auto& value = row.history[profiler.LastFrame()];
			passed &= value.selfMs <= value.inclusiveMs && value.calls == 1;
			foundChild |= row.detail && row.parent >= 0 && row.name == "移動";
			foundException |= row.name == "例外";
		}
		passed &= foundChild && foundException;
		const auto snapshot = profiler.Capture();
		const size_t rows = profiler.Rows().size();
		profiler.Configure(false, "Example.Rain", ScriptProfiler::OwnerID(first));
		passed &= profiler.Rows().size() == rows && profiler.BeginDetail(entity, 11, "停止") == 0;
		profiler.Configure(true, "Example.Rain", 0);
		const uint64_t stale = profiler.BeginDetail(entity, 11, "古い区間");
		profiler.Clear();
		profiler.BeginFrame();
		const uint64_t current = profiler.BeginDetail(entity, 12, "新しい区間");
		profiler.End(stale, true);
		profiler.End(current, true);
		profiler.EndFrame();
		passed &= profiler.Rows().size() == 1 &&
			profiler.Rows()[0].history[profiler.LastFrame()].calls == 1;
		for (int i = 0; i < 305; ++i) {
			profiler.BeginFrame();
			profiler.EndFrame();
		}
		passed &= profiler.FrameCount() == 300 &&
			profiler.Rows()[0].history[profiler.LastFrame()].calls == 0;
		profiler.Clear();
		for (int i = 0; i < 4100; ++i) {
			const std::string name = std::to_string(i);
			profiler.End(profiler.BeginDetail(entity, 11, name.c_str()), true);
		}
		passed &= profiler.Rows().size() == 4096 && profiler.IsOverflowed();
		profiler.Configure(false, {}, 0);
		profiler.ResetOwners();
		passed &= profiler.Owners().empty() && profiler.Rows().empty();
		passed &= !snapshot.rows.empty() && snapshot.frameCount == 1 && snapshot.rows[0].current.calls == 1;
		return passed;
	}

	bool TestScriptExecutionOrderSettings() {

		constexpr std::string_view scriptTypeID =
			"00000000000000000000000000000001";
		Engine::BehaviorTypeRegistry& registry =
			Engine::BehaviorTypeRegistry::GetInstance();
		Engine::ScriptExecutionOrderSettings::RemoveOverride(scriptTypeID);
		const uint32_t typeID = registry.RegisterManaged(
			scriptTypeID, "Tests.ExecutionOrder", "ExecutionOrder", {}, 25);

		bool passed = registry.GetInfo(typeID).defaultExecutionOrder == 25 &&
			registry.GetInfo(typeID).executionOrder == 25;
		const uint64_t revision = registry.GetExecutionOrderRevision();
		passed &= Engine::ScriptExecutionOrderSettings::SetOverride(scriptTypeID, -100);
		registry.RefreshManagedExecutionOrders();
		passed &= registry.GetInfo(typeID).executionOrder == -100 &&
			registry.GetExecutionOrderRevision() != revision;

		passed &= Engine::ScriptExecutionOrderSettings::RemoveOverride(scriptTypeID);
		registry.RefreshManagedExecutionOrders();
		passed &= registry.GetInfo(typeID).executionOrder == 25;
		registry.ClearManaged();
		return passed;
	}
}
