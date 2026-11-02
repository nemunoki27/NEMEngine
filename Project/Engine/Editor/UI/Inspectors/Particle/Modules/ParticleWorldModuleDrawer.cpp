#include "ParticleWorldModuleDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Builtin/ParticleWorldModules.h>
#include <Engine/Editor/UI/Inspectors/Particle/GUI/ParticleGUIHelpers.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

using namespace Engine;

namespace {

	// 共有する場と衝突対象のマスクを選ぶ
	bool DrawLayerMask(const EditorPanelContext& context, uint32_t& mask, bool collision = false) {

		return DrawInspectorMask(context, collision ? "衝突対象マスク" : "対象作用グループ", mask,
			collision ? InspectorMaskDomain::Collision : InspectorMaskDomain::Particle).valueChanged;
	}

	// 保存参照を保ちながらHierarchyのEntityで編集する
	bool DrawReference(const EditorPanelContext& context, const char* label, SceneEntityReference& reference) {

		auto* world = context.GetWorld();
		if (!world) return false;
		const auto owner = context.editorState ? context.editorState->selectedEntity : Entity::Null();
		return SceneEntityReferenceFields::Draw(label, *world, owner, reference).valueChanged;
	}

	// 力場の種類と共有範囲を編集する
	bool DrawForce(const EditorPanelContext& context, ParticleForceFieldSettings& settings) {

		bool changed = MyGUI::Checkbox("Sceneの力場を使う", settings.useSceneFields);
		changed |= MyGUI::DragFloat("強さ", settings.strength).valueChanged;
		if (settings.useSceneFields) changed |= DrawLayerMask(context, settings.layerMask);
		if (!settings.useSceneFields) {
			changed |= MyGUI::EnumCombo("種類", settings.type).valueChanged;
			changed |= MyGUI::DragVector3("中心", settings.center).valueChanged;
			changed |= MyGUI::DragVector3("軸方向", settings.axis).valueChanged;
			changed |= MyGUI::DragFloat("範囲", settings.range, ParticleGUI::MakeDragSetting(0.001f, 100000)).valueChanged;
			changed |= MyGUI::DragFloat("減衰", settings.falloff, ParticleGUI::MakeDragSetting(0, 16)).valueChanged;
		}
		return changed;
	}

	// 弧長に沿う移動と接線姿勢を編集する
	bool DrawSpline(const EditorPanelContext& context, ParticleSplineMovementSettings& settings) {

		bool changed = DrawReference(context, "Spline", settings.spline);
		changed |= MyGUI::DragFloat("速度", settings.speed).valueChanged;
		changed |= MyGUI::DragFloat("開始距離", settings.startDistance).valueChanged;
		changed |= MyGUI::DragFloat("周回半径", settings.orbitRadius, ParticleGUI::MakeDragSetting(0, 10000)).valueChanged;
		changed |= MyGUI::DragFloat("距離あたりの周回数", settings.orbitTurnsPerMeter).valueChanged;
		changed |= MyGUI::Checkbox(Engine::GetInspectorLabel(Engine::InspectorFieldID::Loop), settings.loop);
		changed |= MyGUI::Checkbox("接線方向へ向ける", settings.orientToTangent);
		return changed;
	}

	// 移動後に実行する衝突と応答を編集する
	bool DrawCollision(const EditorPanelContext& context, ParticleCollisionSettings& settings) {

		bool changed = MyGUI::Checkbox(Engine::GetInspectorLabel(Engine::InspectorFieldID::Enabled), settings.enabled);
		changed |= MyGUI::Checkbox("CollisionComponent", settings.sceneColliders);
		changed |= MyGUI::Checkbox("Triggerを含む", settings.includeTriggers);
		changed |= MyGUI::Checkbox("平面", settings.plane);
		if (settings.plane) {
			changed |= MyGUI::DragVector3("平面の法線", settings.planeNormal).valueChanged;
			changed |= MyGUI::DragFloat("平面の距離", settings.planeDistance).valueChanged;
		}
		changed |= MyGUI::Checkbox("Heightmap", settings.heightmap);
		if (settings.sceneColliders || settings.heightmap) {
			changed |= DrawLayerMask(context, settings.layerMask, settings.sceneColliders);
			if (settings.sceneColliders && settings.heightmap)
				MyGUI::Text("対象マスク", "高さ画像の作用グループにも同じビットを使用");
		}
		changed |= MyGUI::Checkbox("Camera深度", settings.depth);
		if (settings.depth) {
			changed |= DrawReference(context, "判定Camera", settings.depthCamera);
			ImGui::TextWrapped("未指定はメインGame Cameraを使用します");
			changed |= MyGUI::DragFloat("深度の厚み", settings.depthThickness, ParticleGUI::MakeDragSetting(0.001f, 100)).valueChanged;
		}
		changed |= MyGUI::DragFloat("粒子半径", settings.radius, ParticleGUI::MakeDragSetting(0, 100)).valueChanged;
		changed |= MyGUI::DragFloat("反発", settings.bounce, ParticleGUI::MakeDragSetting(0, 1)).valueChanged;
		changed |= MyGUI::DragFloat(Engine::GetInspectorLabel(Engine::InspectorFieldID::Friction), settings.friction, ParticleGUI::MakeDragSetting(0, 1)).valueChanged;
		changed |= MyGUI::EnumCombo("応答", settings.response).valueChanged;
		return changed;
	}
}

bool ParticleWorldModuleDrawer::Draw(IParticleModule& module) {

	return Draw(module, EditorPanelContext{});
}

bool ParticleWorldModuleDrawer::Draw(IParticleModule& module, const EditorPanelContext& context) {

	bool changed = false;
	if (auto* force = dynamic_cast<ParticleForceFieldModule*>(&module)) {
		auto settings = force->GetSettings();
		changed = DrawForce(context, settings);
		if (changed) force->SetSettings(settings);
	} else if (auto* vector = dynamic_cast<ParticleVectorFieldModule*>(&module)) {
		auto settings = vector->GetSettings();
		changed |= MyGUI::DragFloat("強さ", settings.intensity).valueChanged;
		changed |= MyGUI::DragFloat("追従", settings.tightness, ParticleGUI::MakeDragSetting(0, 1)).valueChanged;
		changed |= DrawLayerMask(context, settings.layerMask);
		if (changed) vector->SetSettings(settings);
	} else if (auto* spline = dynamic_cast<ParticleSplineMovementModule*>(&module)) {
		auto settings = spline->GetSettings();
		changed = DrawSpline(context, settings);
		if (changed) spline->SetSettings(settings);
	} else if (auto* collision = dynamic_cast<ParticleCollisionModule*>(&module)) {
		auto settings = collision->GetSettings();
		changed = DrawCollision(context, settings);
		if (changed) collision->SetSettings(settings);
	}
	return changed;
}
