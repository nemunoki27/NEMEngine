#include "TestContracts.h"
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/Scripting/Managed/ScriptFieldStorage.h>
#include <Engine/Core/World/Scene/Serialization/RuntimeEntitySnapshot.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
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
#include <stdexcept>
#include <utility>

namespace NEMTests {

	bool TestManagedLifecycleIntegration() {

		using namespace Engine;
		auto& runtime = ManagedScriptRuntime::GetInstance();
		if (!runtime.Init() || !runtime.ReloadGameAssembly(false)) return false;
		const auto* type = BehaviorTypeRegistry::GetInstance().FindByName("SandboxScripts.RuntimeLifecycleProbe");
		if (!type) { runtime.Finalize(); return false; }
		const std::string typeID = type->scriptTypeID;
		ECSWorld world(ECSWorldKind::Runtime);
		const auto worldHandle = ManagedWorldRegistry::GetInstance().Register(world);
		SystemContext context{};
		context.world = &world;
		context.mode = WorldMode::Play;
		BehaviorSystem behaviors;
		behaviors.OnWorldEnter(world, context);
		const auto owner = world.CreateEntity();
		MonoBehavior* first = BehaviorSystem::AttachScript(owner, typeID, context);
		MonoBehavior* second = BehaviorSystem::AttachScript(owner, typeID, context);
		const char* maskID = "6ae09a0e-2e17-4405-ae7c-2f39a2d7d39b";
		const auto hasMask = [maskID](MonoBehavior* instance, int expected) {

			const auto state = instance ? instance->GetRuntimeSerializedState() : nlohmann::json::object();
			if (state.value(maskID, -1) == expected) return true;
			std::cerr << "Lifecycle mask expected=" << expected << " instance=" << instance << " state=" << state.dump() << '\n';
			return false;
		};
		// 返却前のAwakeとOnEnable、同型の別個体を確認する
		bool valid = first && second && first != second && hasMask(first, 3) && hasMask(second, 3);
		behaviors.Update(world, context);
		valid &= hasMask(first, 7) && hasMask(second, 7);
		if (first) {
			// Inspector値とは別に、複製用の現在値と欠損Fieldを取得する
			const char* valueID = "bda98a95-8333-4c86-b4d1-0320f6e3032a";
			const char* missingID = "5b1792d7-a9cc-41cc-a5c3-d754ae083fab";
			first->SetSerializedFields({ { "fields", { { missingID, { { "value", "保存を維持" } } } } } });
			first->SetRuntimeSerializedField(world, valueID, 17.0f);
			nlohmann::json saved;
			const bool captured = first->CaptureSavedFields(world, saved);
			const auto values = ScriptFieldStorage::ExtractValues(saved);
			const bool savedValid = captured && values.value(valueID, 0.0f) == 17.0f &&
				values.value(missingID, std::string{}) == "保存を維持";
			if (!savedValid) {
				std::cerr << "Saved fields captured=" << captured << " state=" << saved.dump() << '\n';
			}
			valid &= savedValid;
			const auto retained = saved;
			valid &= !runtime.CaptureSavedValueMap({}, world, saved) && saved == retained;
		}
		const auto entries = GetScriptEntries(world, owner);
		if (entries.size() != 2) valid = false;
		if (entries.size() == 2) {
			const auto removedSlot = entries[0].scriptSlotID;
			const auto remainingSlot = entries[1].scriptSlotID;
			BehaviorSystem::SetScriptEnabled(owner, removedSlot, false);
			EntityTreeSnapshot snapshot;
			const bool captured = RuntimeEntitySnapshot::Capture(world, owner, snapshot);
			if (captured) {
				const auto& scripts = snapshot.entities[0].components["Script"];
				valid &= scripts.size() == 2 && scripts[0]["enabled"] == false &&
					scripts[0]["serializedFields"]["fields"]["bda98a95-8333-4c86-b4d1-0320f6e3032a"]["value"] == 17.0f;
			} else {
				std::cerr << "Runtime entity snapshot failed\n";
				valid = false;
			}
			BehaviorSystem::SetScriptEnabled(owner, removedSlot, true);
			ECSWorld otherWorld;
			bool enabled = false;
			nlohmann::json unrelated = { { "retained", true } };
			valid &= !BehaviorSystem::CaptureSavedFields(otherWorld, owner, removedSlot, unrelated, enabled) &&
				unrelated == nlohmann::json({ { "retained", true } }) && !enabled;
			world.GetCommandBuffer().EnqueueRemoveScript(owner, removedSlot);
			world.GetCommandBuffer().Flush(world);
			behaviors.Update(world, context);
			valid &= !BehaviorSystem::FindRuntimeHandle(owner, removedSlot).IsValid() &&
				BehaviorSystem::FindRuntimeHandle(owner, remainingSlot).IsValid();
		}
		// inactiveへの追加は有効化までAwakeを待つ
		const auto inactive = world.CreateEntity();
		auto& state = world.AddComponent<SceneObjectComponent>(inactive);
		state.activeSelf = state.activeInHierarchy = false;
		MonoBehavior* delayed = BehaviorSystem::AttachScript(inactive, typeID, context);
		valid &= hasMask(delayed, 0);
		world.GetComponent<SceneObjectComponent>(inactive).activeSelf = true;
		world.GetComponent<SceneObjectComponent>(inactive).activeInHierarchy = true;
		world.MarkComponentModified<SceneObjectComponent>(inactive);
		behaviors.Update(world, context);
		valid &= hasMask(delayed, 7);
		behaviors.OnWorldExit(world, context);
		ManagedWorldRegistry::GetInstance().Unregister(worldHandle);
		runtime.Finalize();
		return valid;
	}

	bool TestScriptFieldStorage() {

		using namespace Engine;
		const nlohmann::json graph = { { "1", { { "type", "Unknown" }, { "value", 42 } } } };
		nlohmann::json stored = {
			{ "version", 3 },
			{ "fields", {
				{ "known", { { "name", "Speed" }, { "value", 1 } } },
				{ "missing", { { "name", "OldField" }, { "value", "keep" } } },
				{ "reference", { { "value", { { "$ref", "1" } } } } }
			} },
			{ "$managedReferences", graph }
		};
		auto values = ScriptFieldStorage::ExtractValues(stored);
		if (values["known"] != 1 || values["missing"] != "keep" || values["$managedReferences"] != graph) {
			return false;
		}
		values["known"] = 17;
		ScriptFieldStorage::MergeValues(stored, values);
		if (stored["version"] != 3 || stored["fields"]["known"]["name"] != "Speed" ||
			stored["fields"]["known"]["value"] != 17 || stored["fields"]["missing"]["name"] != "OldField" ||
			stored["$managedReferences"] != graph || ScriptFieldStorage::ExtractValues(stored) != values) {
			return false;
		}
		const auto retained = stored;
		bool rejected = false;
		try {
			ScriptFieldStorage::MergeValues(stored, nlohmann::json::array());
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		return rejected && stored == retained;
	}

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
