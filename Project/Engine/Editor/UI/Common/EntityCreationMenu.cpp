#include "EntityCreationMenu.h"

#include <Engine/Editor/Commands/Entity/CreateEntityCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

#include <imgui.h>

namespace Engine::EntityCreationMenu {

	void CreateEntityFromMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID, const char* name,
		Engine::EntityCreationPreset preset, Engine::Dimension dimension) {

		context.host->ExecuteEditorCommand(
			std::make_unique<Engine::CreateEntityCommand>(
				name, parentStableUUID, preset, dimension));
	}

	void DrawUICreationMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID) {

		if (!ImGui::BeginMenu("UI", context.CanEditScene())) {
			return;
		}
		auto create = [&](const char* label, const char* name, Engine::EntityCreationPreset preset) {
			if (ImGui::MenuItem(label)) {
				CreateEntityFromMenu(context, parentStableUUID,
					name, preset, Engine::Dimension::Type2D);
			}
		};
		create("Canvas", "Canvas", Engine::EntityCreationPreset::Canvas);
		ImGui::Separator();
		create("Image", "Image", Engine::EntityCreationPreset::UIImage);
		create("Text", "Text", Engine::EntityCreationPreset::UIText);
		create("Image Button", "Image Button", Engine::EntityCreationPreset::UIImageButton);
		create("Text Button", "Text Button", Engine::EntityCreationPreset::UITextButton);
		create("Progress", "Progress", Engine::EntityCreationPreset::UIProgress);
		ImGui::EndMenu();
	}

	void DrawCameraCreationMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID) {

		if (!ImGui::BeginMenu("Camera", context.CanEditScene())) {
			return;
		}
		if (ImGui::MenuItem("2D")) {
			CreateEntityFromMenu(context, parentStableUUID, "Camera2D",
				Engine::EntityCreationPreset::Camera, Engine::Dimension::Type2D);
		}
		if (ImGui::MenuItem("3D")) {
			CreateEntityFromMenu(context, parentStableUUID, "Camera3D",
				Engine::EntityCreationPreset::Camera, Engine::Dimension::Type3D);
		}
		ImGui::EndMenu();
	}

	void DrawMeshCreationMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID) {

		if (!ImGui::BeginMenu("Mesh", context.CanEditScene())) {
			return;
		}
		if (ImGui::MenuItem("Static")) {
			CreateEntityFromMenu(context, parentStableUUID, "Static Mesh",
				Engine::EntityCreationPreset::StaticMesh, Engine::Dimension::Type3D);
		}
		if (ImGui::MenuItem("Skinned")) {
			CreateEntityFromMenu(context, parentStableUUID, "Skinned Mesh",
				Engine::EntityCreationPreset::SkinnedMesh, Engine::Dimension::Type3D);
		}
		ImGui::EndMenu();
	}

	void DrawPrimitiveShapeMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID, Engine::Dimension dimension) {

		auto create = [&](const char* label, Engine::EntityCreationPreset preset) {
			if (ImGui::MenuItem(label)) {
				CreateEntityFromMenu(context, parentStableUUID,
					label, preset, dimension);
			}
		};
		create("Plane", Engine::EntityCreationPreset::PrimitivePlane);
		if (dimension == Engine::Dimension::Type2D) {
			create("Ring", Engine::EntityCreationPreset::PrimitiveRing);
			return;
		}
		create("Cross Plane", Engine::EntityCreationPreset::PrimitiveCrossPlane);
		create("Ring", Engine::EntityCreationPreset::PrimitiveRing);
		create("Cylinder", Engine::EntityCreationPreset::PrimitiveCylinder);
		create("Sphere", Engine::EntityCreationPreset::PrimitiveSphere);
		create("Hemisphere", Engine::EntityCreationPreset::PrimitiveHemisphere);
		create("Cube", Engine::EntityCreationPreset::PrimitiveCube);
	}

	void DrawPrimitiveCreationMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID) {

		if (!ImGui::BeginMenu("Primitive", context.CanEditScene())) {
			return;
		}
		if (ImGui::BeginMenu("2D")) {
			DrawPrimitiveShapeMenu(
				context, parentStableUUID, Engine::Dimension::Type2D);
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("3D")) {
			DrawPrimitiveShapeMenu(
				context, parentStableUUID, Engine::Dimension::Type3D);
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	void DrawParticleCreationMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID) {

		if (!ImGui::BeginMenu("Particle", context.CanEditScene())) {
			return;
		}
		if (ImGui::MenuItem("2D")) {
			CreateEntityFromMenu(context, parentStableUUID, "Particle2D",
				Engine::EntityCreationPreset::Particle, Engine::Dimension::Type2D);
		}
		if (ImGui::MenuItem("3D")) {
			CreateEntityFromMenu(context, parentStableUUID, "Particle3D",
				Engine::EntityCreationPreset::Particle, Engine::Dimension::Type3D);
		}
		ImGui::EndMenu();
	}

	void DrawLightCreationMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID) {

		if (!ImGui::BeginMenu("Light", context.CanEditScene())) {
			return;
		}
		if (ImGui::MenuItem("Directional")) {
			CreateEntityFromMenu(context, parentStableUUID, "Directional",
				Engine::EntityCreationPreset::DirectionalLight, Engine::Dimension::Type3D);
		}
		if (ImGui::MenuItem("Point")) {
			CreateEntityFromMenu(context, parentStableUUID, "Point",
				Engine::EntityCreationPreset::PointLight, Engine::Dimension::Type3D);
		}
		if (ImGui::MenuItem("Spot")) {
			CreateEntityFromMenu(context, parentStableUUID, "Spot",
				Engine::EntityCreationPreset::SpotLight, Engine::Dimension::Type3D);
		}
		if (ImGui::MenuItem("Rect")) {
			CreateEntityFromMenu(context, parentStableUUID, "Rect",
				Engine::EntityCreationPreset::RectLight, Engine::Dimension::Type3D);
		}
		ImGui::EndMenu();
	}

	void DrawEntityCreationMenu(const Engine::EditorPanelContext& context,
		Engine::UUID parentStableUUID, const char* label,
		Engine::Dimension defaultDimension) {

		if (!ImGui::BeginMenu(label, context.CanEditScene())) {
			return;
		}
		if (ImGui::MenuItem("空オブジェクト")) {
			CreateEntityFromMenu(context, parentStableUUID, "Entity",
				Engine::EntityCreationPreset::Empty, defaultDimension);
		}
		ImGui::Separator();
		DrawCameraCreationMenu(context, parentStableUUID);
		DrawMeshCreationMenu(context, parentStableUUID);
		DrawPrimitiveCreationMenu(context, parentStableUUID);
		DrawParticleCreationMenu(context, parentStableUUID);
		DrawLightCreationMenu(context, parentStableUUID);
		DrawUICreationMenu(context, parentStableUUID);
		ImGui::EndMenu();
	}

}
