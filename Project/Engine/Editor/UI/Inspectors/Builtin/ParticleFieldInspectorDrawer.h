#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/ParticleFieldComponents.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>

namespace Engine {

	class ForceFieldInspectorDrawer : public SerializedComponentInspectorDrawer<ForceFieldComponent> {
	public:

		ForceFieldInspectorDrawer() : SerializedComponentInspectorDrawer("Force Field", "ForceField") {}
	private:

		// 場の強さと種類を編集する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
	};

	class VectorFieldInspectorDrawer : public SerializedComponentInspectorDrawer<VectorFieldComponent> {
	public:

		VectorFieldInspectorDrawer() : SerializedComponentInspectorDrawer("Vector Field", "VectorField") {}
	private:

		// FGA参照と変換範囲を編集する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
	};

	class HeightmapColliderInspectorDrawer : public SerializedComponentInspectorDrawer<HeightmapColliderComponent> {
	public:

		HeightmapColliderInspectorDrawer() : SerializedComponentInspectorDrawer("Heightmap Collider", "HeightmapCollider") {}
	private:

		// 高さ画像と地形範囲を編集する
		void DrawFields(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool& active) override;
	};
}
