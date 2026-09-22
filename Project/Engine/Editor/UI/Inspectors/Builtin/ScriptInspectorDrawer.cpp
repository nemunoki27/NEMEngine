#include "ScriptInspectorDrawer.h"
#include "Script/ScriptFieldInspector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptBuildService.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Editor/Commands/Components/ApplyRuntimeToAuthoringCommand.h>
#include <Engine/Editor/Commands/Components/RemoveComponentCommand.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
// c++
#include <charconv>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>

//============================================================================
//	ScriptInspectorDrawer classMethods
//============================================================================
using namespace Engine::ScriptFieldInspector;

void Engine::ScriptInspectorDrawer::OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const ScriptComponent&) {

	const std::span<const ScriptEntry> entries =
		GetScriptEntries(static_cast<const ECSWorld&>(world), entity);
	draftScripts_.assign(entries.begin(), entries.end());
}

void Engine::ScriptInspectorDrawer::SerializeDraft(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] const ScriptComponent& component, nlohmann::json& out) const {

	SerializeScriptEntries(draftScripts_, out);
}

void Engine::ScriptInspectorDrawer::ApplyPreview(
	ECSWorld& world, const Entity& entity, [[maybe_unused]] const ScriptComponent& previewComponent) {

	if (!world.IsAlive(entity) || !world.HasComponent<ScriptComponent>(entity)) {
		return;
	}

	// 可変長データは設定コンポーネントとは別のECS Bufferへまとめて反映する
	SetScriptEntries(world, entity, draftScripts_);
	world.MarkComponentModified<ScriptComponent>(entity);
	world.MarkComponentModified<ScriptEntry>(entity);
}

void Engine::ScriptInspectorDrawer::DrawFields(const EditorPanelContext& context,
	ECSWorld& world, const Entity& entity, bool& anyItemActive) {

	auto& runtime = ManagedScriptRuntime::GetInstance();
	const bool playing = context.IsPlaying();

	runtimeValues_.BeginFrame(playing);

	int32_t removeIndex = -1;
	int32_t moveUpIndex = -1;
	int32_t moveDownIndex = -1;
	ManagedScriptBuildService::Snapshot buildSnapshot{};
	const bool hasBuildService = context.editorContext && context.editorContext->scriptBuildService;
	if (hasBuildService) {
		buildSnapshot = context.editorContext->scriptBuildService->GetSnapshot();
	}
	const ManagedBuildDiagnostic* globalBuildError = FindGlobalBuildError(buildSnapshot.buildID);
	for (size_t i = 0; i < draftScripts_.size(); ++i) {

		ImGui::PushID(static_cast<int32_t>(i));
		ScriptEntry& entry = draftScripts_[i];

		// 型の解決状態はレジストリで判定する
		const ManagedBuildDiagnostic* sourceBuildError = FindSourceBuildError(entry,
			context.editorContext ? context.editorContext->assetDatabase : nullptr, buildSnapshot.buildID);
		const ManagedScriptResolutionReason resolutionReason = ResolveScriptReason(
			entry, sourceBuildError != nullptr, globalBuildError != nullptr);

		// 未登録のときだけ欠落表示にする
		std::string headerText;
		if (resolutionReason == ManagedScriptResolutionReason::BuildFailed) {
			headerText = entry.lastKnownTypeName.empty()
				? "スクリプト読込失敗"
				: ("スクリプト読込失敗 (" + ScriptTypeShortName(entry.lastKnownTypeName) + ")");
		} else if (resolutionReason == ManagedScriptResolutionReason::TypeNotRegistered) {
			headerText = entry.lastKnownTypeName.empty()
				? "スクリプトなし"
				: ("スクリプトなし (" + ScriptTypeShortName(entry.lastKnownTypeName) + ")");
		} else if (!entry.lastKnownTypeName.empty()) {
			headerText = ScriptTypeShortName(entry.lastKnownTypeName);
		} else if (resolutionReason == ManagedScriptResolutionReason::Unassigned) {
			headerText = "スクリプト未設定";
		} else {
			headerText = "Script " + std::to_string(i);
		}

		const std::string headerID = headerText + "##ScriptEntry";
		const bool headerOpen = MyGUI::CollapsingHeader(headerID.c_str());
		if (ImGui::BeginPopupContextItem()) {

			if (ImGui::MenuItem("スクリプトを削除")) {
				removeIndex = static_cast<int32_t>(i);
			}
			ImGui::EndPopup();
		}
		if (headerOpen) {

			DrawField(anyItemActive, [&]() {
				return InspectorDrawerCommon::DrawCheckboxField("有効", entry.enabled);
				});

			// 解決済みかはレジストリで判定しフィールド数では判定しない
			const ManagedScriptSchema& schema = runtime.GetScriptSchema(entry.scriptTypeID);
			const bool resolved = (resolutionReason == ManagedScriptResolutionReason::Resolved);

			// 解決済みかつフィールドがある場合のみ描画する
			if (resolved && !schema.fields.empty()) {

				EnsureAuthoringSchema(entry, schema);

				DrawContext ctx{};
				ctx.panel = &context;
				ctx.world = &world;

				if (playing) {

					// 実行中は実体の値を表示編集し保存しない
					ImGui::TextDisabled("Runtime 値");
					const BehaviorHandle handle = FindLiveHandle(world, entity, entry.scriptSlotID);

					nlohmann::json& state = runtimeValues_.GetState(handle);

					ctx.readOnly = false;
					for (const ManagedFieldSchema& field : schema.fields) {
						DrawRuntimeField(field, state, ctx, handle);
					}
				} else {

					// 編集中は編集用の値を扱う
					bool changed = false;
					for (const ManagedFieldSchema& field : schema.fields) {
						changed |= DrawAuthoringField(field, entry.serializedFields, ctx, anyItemActive);
					}
					if (changed) {
						RequestCommit();
					}
				}
			} else if (resolutionReason == ManagedScriptResolutionReason::BuildFailed ||
				resolutionReason == ManagedScriptResolutionReason::TypeNotRegistered ||
				resolutionReason == ManagedScriptResolutionReason::Unassigned) {
				// 未登録のときだけ欠落表示し値は保持する
				ImGui::TextDisabled("スクリプト型を読み込めません。保存済みの値は保持されます。");
				ImGui::BulletText("直前の型: %s",
					entry.lastKnownTypeName.empty() ? "不明" : entry.lastKnownTypeName.c_str());
				ImGui::BulletText("スクリプト型ID: %s", entry.scriptTypeID.c_str());
				ImGui::BulletText("ソースアセット: %s", ToString(entry.scriptAsset).c_str());
				ImGui::BulletText("スロットID: %016llx", static_cast<unsigned long long>(entry.scriptSlotID.value));
				ImGui::BulletText("理由: %s", ResolutionReasonLabel(resolutionReason));
				if (resolutionReason == ManagedScriptResolutionReason::BuildFailed) {
					const ManagedBuildDiagnostic* diagnostic = sourceBuildError ? sourceBuildError : globalBuildError;
					if (diagnostic) {
						ImGui::TextWrapped("ビルドエラー %s (%d:%d): %s",
							diagnostic->code.c_str(), diagnostic->line, diagnostic->column,
							diagnostic->message.c_str());
					} else {
						ImGui::TextWrapped("ビルドエラー: %s", buildSnapshot.lastFailureSummary.c_str());
					}
				}
				if (ImGui::SmallButton("GUID をコピー")) {
					ImGui::SetClipboardText(entry.scriptTypeID.c_str());
				}
			}

			// 未解決フィールドは保持して表示する
			DrawUnresolved(entry.serializedFields);

			// 並べ替えは同一エンティティ内のスロット順
			ImGui::BeginDisabled(i == 0);
			if (ImGui::SmallButton("▲")) {
				moveUpIndex = static_cast<int32_t>(i);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(i + 1 >= draftScripts_.size());
			if (ImGui::SmallButton("▼")) {
				moveDownIndex = static_cast<int32_t>(i);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("スクリプトを削除")) {
				removeIndex = static_cast<int32_t>(i);
			}
		}
		ImGui::Separator();
		ImGui::PopID();
	}

	if (0 <= removeIndex) {

		// 最後のスクリプトなら内部コンテナも残さず削除する
		if (draftScripts_.size() == 1 && context.host) {
			context.host->ExecuteEditorCommand(
				std::make_unique<RemoveComponentCommand>(entity, ScriptComponent::kTypeName));
			draftScripts_.clear();
			return;
		}
		draftScripts_.erase(draftScripts_.begin() + removeIndex);
		RequestCommit();
	}
	// スロット並べ替えは入れ替えて確定しUndoできる
	else if (0 < moveUpIndex && moveUpIndex < static_cast<int32_t>(draftScripts_.size())) {
		std::swap(draftScripts_[moveUpIndex], draftScripts_[moveUpIndex - 1]);
		RequestCommit();
	} else if (0 <= moveDownIndex && moveDownIndex + 1 < static_cast<int32_t>(draftScripts_.size())) {
		std::swap(draftScripts_[moveDownIndex], draftScripts_[moveDownIndex + 1]);
		RequestCommit();
	}
}
