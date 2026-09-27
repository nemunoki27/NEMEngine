#include "CommandHistoryTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Command/CommandHistory.h>
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>
#include <Engine/Editor/Commands/Core/EditorCommandExecution.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Core/EditorSceneDirtyState.h>

// c++
#include <stdexcept>

namespace {

	struct CommandContext {

		int value = 0;
		bool failExecute = false;
		bool failUndo = false;
		bool failRedo = false;
		bool rejectRedo = false;
		bool reenter = false;
		bool rejectedReentry = false;
		Engine::CommandHistory<CommandContext>* history = nullptr;
	};

	class CounterCommand final : public Engine::ICommand<CommandContext> {
	public:

		bool Execute(CommandContext& context) override {

			if (context.failExecute) {
				throw std::runtime_error("実行失敗");
			}
			if (context.reenter) {
				context.reenter = false;
				context.rejectedReentry = !context.history->Execute(std::make_unique<CounterCommand>(), context) &&
					!context.history->Undo(context) && !context.history->Redo(context);
				try {
					context.history->Clear();
					context.rejectedReentry = false;
				} catch (const std::logic_error&) {
				}
			}
			++context.value;
			return true;
		}

		void Undo(CommandContext& context) override {

			if (context.failUndo) {
				throw std::runtime_error("取消失敗");
			}
			--context.value;
		}

		bool Redo(CommandContext& context) override {

			if (context.failRedo) {
				throw std::runtime_error("再実行失敗");
			}
			if (context.rejectRedo) {
				return false;
			}
			return Execute(context);
		}

		const char* GetName() const override { return "Counter"; }
	};

	struct EditTestState {

		int value = 0;
		int failExecute = 0;
		int failUndo = 0;
		bool rejectExecute = false;
	};

	class EditCommand final : public Engine::IEditorCommand {
	public:

		EditCommand(EditTestState& state, int value) : state_(state), value_(value) {}

		bool Execute(Engine::EditorCommandContext& context) override {

			if (state_.failExecute == value_) {
				if (state_.rejectExecute) {
					return false;
				}
				throw std::runtime_error("一括操作の途中失敗");
			}
			state_.value += value_;
			context.editorState->SelectEntity({ static_cast<uint32_t>(value_), 1 });
			return true;
		}

		void Undo(Engine::EditorCommandContext& context) override {

			if (state_.failUndo == value_) {
				throw std::runtime_error("一括取消の途中失敗");
			}
			state_.value -= value_;
			context.editorState->SelectEntity(Engine::Entity::Null());
		}

		const char* GetName() const override { return "Edit"; }
	private:
		EditTestState& state_;
		int value_;
	};

	std::unique_ptr<Engine::CompositeEditorCommand> MakeComposite(EditTestState& state) {

		std::vector<std::unique_ptr<Engine::IEditorCommand>> commands;
		for (int value = 1; value <= 3; ++value) {
			commands.emplace_back(std::make_unique<EditCommand>(state, value));
		}
		return std::make_unique<Engine::CompositeEditorCommand>(std::move(commands));
	}
}

bool NEMTests::TestCommandHistoryFailures() {

	Engine::CommandHistory<CommandContext> history;
	CommandContext context;
	context.history = &history;
	if (!history.Execute(std::make_unique<CounterCommand>(), context)) {
		return false;
	}
	const auto* original = history.PeekUndo();
	context.failUndo = true;
	bool failed = false;
	try {
		history.Undo(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	// Undo例外後も元の履歴と値を保つ
	if (!failed || history.PeekUndo() != original || history.CanRedo() || context.value != 1) {
		return false;
	}
	context.failUndo = false;
	if (!history.Undo(context) || context.value != 0) {
		return false;
	}
	context.failRedo = true;
	failed = false;
	try {
		history.Redo(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	// Redo例外とfalseの両方で再試行できる
	if (!failed || history.PeekRedo() != original || history.CanUndo() || context.value != 0) {
		return false;
	}
	context.failRedo = false;
	context.rejectRedo = true;
	if (history.Redo(context) || history.PeekRedo() != original) {
		return false;
	}
	context.rejectRedo = false;
	context.failExecute = true;
	failed = false;
	try {
		history.Execute(std::make_unique<CounterCommand>(), context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	if (!failed || history.PeekRedo() != original || history.CanUndo()) {
		return false;
	}
	// Callbackから同じ履歴へ再入しても実行中Commandを破棄しない
	context.failExecute = false;
	context.reenter = true;
	return history.Redo(context) && context.rejectedReentry && context.value == 1 &&
		history.GetUndoCount() == 1 && !history.CanRedo() && history.Undo(context) && context.value == 0;
}

bool NEMTests::TestCompositeCommandFailures() {

	Engine::EditorState editor;
	Engine::EditorCommandContext context;
	context.editorState = &editor;
	EditTestState state;
	const Engine::Entity initial{ 9, 1 };
	editor.SelectEntity(initial);
	state.failExecute = 2;
	bool failed = false;
	try {
		editor.commandHistory.Execute(MakeComposite(state), context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	// 初回実行の途中例外では値と選択を戻し、履歴へ積まない
	if (!failed || state.value != 0 || editor.selectedEntity != initial || editor.commandHistory.CanUndo()) {
		return false;
	}
	state.rejectExecute = true;
	if (editor.commandHistory.Execute(MakeComposite(state), context) || state.value != 0 ||
		editor.selectedEntity != initial || editor.commandHistory.CanUndo()) {
		return false;
	}
	state.rejectExecute = false;
	state.failExecute = 0;
	if (!editor.commandHistory.Execute(MakeComposite(state), context) || state.value != 6 ||
		editor.commandHistory.GetUndoCount() != 1) {
		return false;
	}
	const Engine::Entity applied{ 3, 1 };
	state.failUndo = 2;
	failed = false;
	try {
		editor.commandHistory.Undo(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	// Undoの途中例外では取消済み部分を再適用する
	if (!failed || state.value != 6 || editor.selectedEntity != applied ||
		editor.commandHistory.GetUndoCount() != 1 || editor.commandHistory.CanRedo()) {
		return false;
	}
	state.failUndo = 0;
	if (!editor.commandHistory.Undo(context) || state.value != 0 || editor.selectedEntity != initial) {
		return false;
	}
	state.failExecute = 2;
	failed = false;
	try {
		editor.commandHistory.Redo(context);
	} catch (const std::runtime_error&) {
		failed = true;
	}
	// Redo失敗後も同じ履歴から再試行できる
	if (!failed || state.value != 0 || editor.selectedEntity != initial ||
		editor.commandHistory.GetRedoCount() != 1 || editor.commandHistory.CanUndo()) {
		return false;
	}
	state.failExecute = 0;
	if (!editor.commandHistory.Redo(context) || state.value != 6 || editor.selectedEntity != applied ||
		editor.commandHistory.GetUndoCount() != 1) {
		return false;
	}

	// Editorの境界では例外を診断し、Play編集をEditのdirtyへ流さない
	Engine::EditorContext runtime;
	runtime.isPlaying = true;
	Engine::EditorSceneDirtyState dirty;
	return Engine::EditorCommandExecution::Run(runtime, editor, dirty,
		[](Engine::EditorCommandContext& current) { return current.allowRuntimeEdit; }) &&
		!Engine::EditorCommandExecution::Run(runtime, editor, dirty,
			[](Engine::EditorCommandContext&) -> bool { throw std::runtime_error("境界の例外処理を検証"); }) &&
		!dirty.HasDirtyScenes() && editor.commandHistory.GetUndoCount() == 1;
}

bool NEMTests::TestEditorSelectionRecovery() {

	Engine::ECSWorld world;
	Engine::EditorState state;
	Engine::EditorContext editor;
	editor.activeWorld = &world;
	Engine::EditorCommandContext context;
	context.editorContext = &editor;
	context.editorState = &state;
	const auto first = world.CreateEntity();
	const auto second = world.CreateEntity();
	const auto id = world.GetUUID(first);
	state.SetSelectedEntities({ first, second });
	state.selectedEntity = first;
	Engine::EditorSelectionSnapshot snapshot;
	snapshot.Capture(context);
	world.DestroyEntity(first);
	world.FlushPendingDestroyEntities();
	const auto restored = world.CreateEntity(id);
	state.ClearSelection();
	// 削除前のhandleではなく復元した実体を選択する
	if (restored == first || !snapshot.Restore(context) || state.selectedEntity != restored ||
		state.selectedEntities != std::vector<Engine::Entity>{ restored, second }) {
		return false;
	}
	const Engine::AssetID asset{ 21, 22 };
	state.SelectAsset(asset);
	snapshot.Capture(context);
	state.SelectEntity(second);
	if (!snapshot.Restore(context) || state.selectionKind != Engine::EditorSelectionKind::Asset ||
		state.selectedAsset != asset) {
		return false;
	}
	state.SelectMeshSubMesh(restored, 3, Engine::UUID{ 7 });
	snapshot.Capture(context);
	state.ClearSelection();
	if (!snapshot.Restore(context) || state.selectionKind != Engine::EditorSelectionKind::MeshSubMesh ||
		state.selectedSubMeshIndex != 3 || state.selectedSubMeshStableID != Engine::UUID{ 7 }) {
		return false;
	}
	state.SelectJoint(restored, 4);
	snapshot.Capture(context);
	state.ClearSelection();
	if (!snapshot.Restore(context) || state.selectedJointSkinnedEntity != restored || state.selectedJointIndex != 4) {
		return false;
	}
	// 別Worldへ切り替えた後は保存していた選択を適用しない
	Engine::ECSWorld other;
	editor.activeWorld = &other;
	state.SelectAsset(asset);
	return !snapshot.Restore(context) && state.selectionKind == Engine::EditorSelectionKind::Asset &&
		state.selectedAsset == asset;
}
