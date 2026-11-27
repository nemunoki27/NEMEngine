#pragma once

#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/Components/Rendering/MaterialPaintComponent.h>
#include <optional>

namespace Engine {

	struct MaterialPaintEdit {
		UUID object;
		std::optional<MaterialPaintComponent> before;
		MaterialPaintComponent after;
	};

	class MaterialPaintCommand final : public IEditorCommand {
	public:
		explicit MaterialPaintCommand(std::vector<MaterialPaintEdit> edits) : edits_(std::move(edits)) {}
		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		const char* GetName() const override { return "マテリアルペイント"; }
	private:
		std::vector<MaterialPaintEdit> edits_;
		bool Apply(EditorCommandContext& context, bool undo);
	};
}
