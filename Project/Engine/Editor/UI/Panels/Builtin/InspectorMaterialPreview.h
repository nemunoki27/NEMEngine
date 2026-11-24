#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolRenderResources.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Effect/ParticleSystem.h>

namespace Engine {

	//============================================================================
	//	InspectorMaterialPreview class
	//	材質の確認に使う独立したワールドと視点を保持する
	//============================================================================
	class InspectorMaterialPreview {
	public:
		void Draw(const EditorPanelContext& context, const MaterialAsset& material);
		void DrawImage(const EditorPanelContext& context, const MaterialAsset& material, ImVec2 size, bool animate);
		void Invalidate() { rendered_ = false; }
	private:
		EditorToolRenderResources resources_;
		std::unique_ptr<ECSWorld> world_;
		std::weak_ptr<const uint8_t> databaseLifetime_;
		AssetID materialID_{};
		Entity root_ = Entity::Null();
		Entity effectSubject_ = Entity::Null();
		ParticleSystem particles_;
		bool trailSubject_ = false;
		int shape_ = 0;
		MaterialDomain domain_ = MaterialDomain::Surface;
		MaterialUsage usage_ = MaterialUsage::Generic;
		float yaw_ = 0.0f;
		float pitch_ = 0.0f;
		float distance_ = 4.3f;
		float time_ = 0.0f;
		bool playing_ = false;
		bool missingFont_ = false;
		bool rendered_ = false;
		bool hasImage_ = false;
		uint64_t textureRevision_ = 0;
		Vector2I pendingSize_{};
		double resizeDeadline_ = 0.0;

		void Rebuild(const EditorPanelContext& context, const MaterialAsset& material);
		void UpdateEffectSubject(SystemContext& system, const MaterialAsset& material);
		void ResetView();
	};
}
