#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>

namespace Engine {

	class ECSWorld;
	struct Entity;

	// 新規エンティティへ追加するビルトイン構成
	enum class EntityCreationPreset :
		uint8_t {

		Empty,
		Canvas,
		UIImage,
		UIText,
		UIButton,
		UIProgress,
	};

	//============================================================================
	//	CreateEntityCommand class
	//	エンティティを作成するコマンド
	//============================================================================
	class CreateEntityCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit CreateEntityCommand(const std::string& name = "Entity", UUID parentStableUUID = UUID{},
			EntityCreationPreset preset = EntityCreationPreset::Empty);
		~CreateEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Create Entity"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::string name_;
		UUID parentStableUUID_{};
		UUID createdStableUUID_{};
		EntityCreationPreset preset_ = EntityCreationPreset::Empty;

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool CreateInternal(EditorCommandContext& context);
		// 作成プリセットのコンポーネントを追加する
		void ApplyPreset(ECSWorld& world, const Entity& entity, const Entity& parent);
	};
} // Engine
