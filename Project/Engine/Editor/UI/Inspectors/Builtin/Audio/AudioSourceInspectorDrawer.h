#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Editor/Assets/Preview/AudioPreviewSession.h>

namespace Engine {

	class ECSWorldLifetime;

	//============================================================================
	//	AudioSourceInspectorDrawer class
	//	AudioSourceコンポーネントのインスペクター描画
	//============================================================================
	class AudioSourceInspectorDrawer :
		public SerializedComponentInspectorDrawer<AudioSourceComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AudioSourceInspectorDrawer() : SerializedComponentInspectorDrawer("Audio Source", "AudioSource") {}
		~AudioSourceInspectorDrawer() = default;
		void SyncPreviewOwner(ECSWorld* world, const Entity& entity) override;
		void EndPreview() override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		AudioPreviewSession preview_;
		std::weak_ptr<const ECSWorldLifetime> previewWorld_;
		Entity previewEntity_ = Entity::Null();
		AssetID previewClip_{};

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;
	};
} // Engine

