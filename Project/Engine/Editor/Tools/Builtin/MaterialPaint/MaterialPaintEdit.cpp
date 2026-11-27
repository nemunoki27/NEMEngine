#include "MaterialPaintEdit.h"

namespace Engine {

	bool MaterialPaintCommand::Apply(EditorCommandContext& context, bool undo) {

		auto* world = context.GetWorld();
		if (!context.CanEditScene() || edits_.empty()) return false;
		for (const auto& edit : edits_) {
			if (!world->IsAlive(world->FindByUUID(edit.object))) return false;
		}
		for (const auto& edit : edits_) {
			const Entity object = world->FindByUUID(edit.object);
			if (undo && !edit.before) {
				world->RemoveComponent<MaterialPaintComponent>(object);
			} else {
				if (!world->HasComponent<MaterialPaintComponent>(object)) world->AddComponent<MaterialPaintComponent>(object);
				world->GetComponent<MaterialPaintComponent>(object) = undo ? *edit.before : edit.after;
				world->MarkComponentModified<MaterialPaintComponent>(object);
			}
		}
		return true;
	}

	bool MaterialPaintCommand::Execute(EditorCommandContext& context) { return Apply(context, false); }

	void MaterialPaintCommand::Undo(EditorCommandContext& context) { Apply(context, true); }
}
