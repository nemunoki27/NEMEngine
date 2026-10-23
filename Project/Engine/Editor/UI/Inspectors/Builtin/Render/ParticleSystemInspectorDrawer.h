#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Editor/UI/Inspectors/Particle/ParticleSettingsEditSession.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

namespace Engine {

	//============================================================================
	//	ParticleSystemInspectorDrawer class
	//	ParticleSystemComponentのインスペクター描画
	//============================================================================
	class ParticleSystemInspectorDrawer :
		public SerializedComponentInspectorDrawer<ParticleSystemComponent> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSystemInspectorDrawer() : SerializedComponentInspectorDrawer("Particle System", "ParticleSystem") {}
		~ParticleSystemInspectorDrawer() = default;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleSettingsEditSession settingsEditor_;
		TextSearchFilter moduleSearch_;
		std::string status_;

		//--------- functions ----------------------------------------------------

		// 設定値をWorldの所有から同期する
		void OnSyncDraftFromWorld(ECSWorld& world, const Entity& entity, const ParticleSystemComponent& component) override;
		// ハンドルを保存せず設定値を履歴へ渡す
		void SerializeDraft(ECSWorld& world, const Entity& entity, const ParticleSystemComponent& component, nlohmann::json& out) const override;
		// Componentと設定を同じ編集として反映する
		void ApplyPreview(ECSWorld& world, const Entity& entity, const ParticleSystemComponent& component) override;
		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine
