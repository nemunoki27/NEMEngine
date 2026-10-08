#include "ScriptExceptionDiagnosticsTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedScriptExceptionStore.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <limits>

#include <json.hpp>

bool NEMTests::CheckScriptExceptionDiagnostics() {

	using namespace Engine;
	auto& registry = ManagedWorldRegistry::GetInstance();
	auto& store = ManagedScriptExceptionStore::GetInstance();
	store.Clear();
	ECSWorld first, second;
	const auto firstOwner = first.CreateEntity();
	const auto secondOwner = second.CreateEntity();
	const auto firstHandle = registry.Register(first);
	const auto secondHandle = registry.Register(second);
	const uint64_t initialSequence = store.ReportSequence();

	// 所有者なしの通知をEntity0へ結び付けない
	store.ReportJSON(R"({"callback":"Timer","message":"失敗"})");
	bool passed = store.Count() == 1 && store.ReportSequence() == initialSequence + 1;
	const auto ownerless = store.Entries().back();
	passed &= ownerless.entityIndex == UINT32_MAX && !ownerless.ResolveOwner(first).IsValid();
	const uint64_t firstID = ownerless.exceptionID;

	// 同じ番号のEntityを持つ別Worldを選択しない
	const nlohmann::json report = {{"worldIndex", firstHandle.index}, {"worldGeneration", firstHandle.generation},
		{"entityIndex", firstOwner.index}, {"entityGeneration", firstOwner.generation}, {"message", "所有者"}};
	store.ReportJSON(report.dump().c_str());
	const auto saved = store.Entries().back();
	passed &= firstOwner == secondOwner && saved.ResolveOwner(first) == firstOwner && !saved.ResolveOwner(second).IsValid() &&
			  saved.exceptionID > firstID;
	registry.Unregister(firstHandle);
	const auto reused = registry.Register(first);
	passed &= reused.index == firstHandle.index && reused.generation != firstHandle.generation &&
			  !saved.ResolveOwner(first).IsValid();

	// 符号と整数の上限を超えた診断番号を拒否する
	store.ReportJSON(R"({"worldIndex":-1,"worldGeneration":4294967296,"entityIndex":18446744073709551615,
		"entityGeneration":-1,"slotId":18446744073709551615,"frames":[{"line":2147483648,"column":-1}]})");
	const auto& malformed = store.Entries().back();
	passed &= malformed.worldIndex == UINT32_MAX && malformed.worldGeneration == 0 && malformed.entityIndex == UINT32_MAX &&
			  malformed.entityGeneration == 0 && malformed.scriptSlotID == std::numeric_limits<uint64_t>::max() &&
			  malformed.frames.size() == 1 && malformed.frames[0].line == 0 && malformed.frames[0].column == 0;

	// UTF8の途中で表示文字列を切らない
	const std::string message = std::string(ManagedScriptExceptionStore::kMaxMessageLength - 1, 'a') + "あ";
	store.ReportJSON(nlohmann::json{{"message", message}}.dump().c_str());
	passed &= store.Entries().back().message == std::string(ManagedScriptExceptionStore::kMaxMessageLength - 1, 'a');
	const uint64_t beforeInvalid = store.ReportSequence();
	const size_t beforeCount = store.Count();
	store.ReportJSON(nullptr);
	store.ReportJSON("{");
	store.ReportJSON(std::string(256 * 1024 + 1, ' ').c_str());
	passed &= store.ReportSequence() == beforeInvalid && store.Count() == beforeCount;

	// 診断が作れない例外だけを補完し、通常結果では停止しない
	passed &= CompleteManagedInvocation(ManagedStatus::Ok, beforeInvalid) == ManagedStatus::Ok &&
			  store.ReportSequence() == beforeInvalid;
	passed &= CompleteManagedInvocation(ManagedStatus::ScriptException, beforeInvalid) == ManagedStatus::ScriptException &&
			  store.ReportSequence() == beforeInvalid + 1 && store.Count() == beforeCount;
	store.ReportJSON(R"({"callback":"Update"})");
	const uint64_t reported = store.ReportSequence();
	CompleteManagedInvocation(ManagedStatus::ScriptException, beforeInvalid + 1);
	passed &= store.ReportSequence() == reported;
	store.Clear();
	passed &= store.ReportSequence() == reported && store.Count() == 0;

	// 履歴上限と表示番号を独立して維持する
	for (size_t index = 0; index <= ManagedScriptExceptionStore::kMaxEntries; ++index) {
		store.ReportJSON(R"({"message":"上限"})");
	}
	passed &= store.Count() == ManagedScriptExceptionStore::kMaxEntries &&
			  store.Entries().front().exceptionID < store.Entries().back().exceptionID;
	store.Clear();
	registry.Unregister(reused);
	registry.Unregister(secondHandle);
	return passed;
}
