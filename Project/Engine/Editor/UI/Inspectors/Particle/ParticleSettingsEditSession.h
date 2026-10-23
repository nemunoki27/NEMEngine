#pragma once

//============================================================================
// include
//============================================================================
#include "Modules/IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine {

	struct ParticleModuleEditCacheEntry {

		UUID instanceID{};
		std::string id;
		ParticleModuleRegistry::TypeID typeID = ParticleModuleRegistry::kInvalidTypeID;
		std::unique_ptr<IParticleModule> module;
		std::unique_ptr<IParticleModuleDrawer> drawer;
	};

	struct ParticleSettingsEditState {

		int32_t selectedPhase = 0;
		std::vector<std::vector<ParticleModuleEditCacheEntry>> moduleCache;
	};

	//============================================================================
	// ParticleSettingsEditSession class
	// Inspectorの設定とModuleの編集状態を保持する
	//============================================================================
	class ParticleSettingsEditSession {
	public:
		//========================================================================
		// public Methods
		//========================================================================

		// 現在の設定へ編集用Moduleを同期する
		IParticleModule* ResolveModuleCache(ParticleModuleEditCacheEntry& cache, const ParticleModuleEntry& entry);

		//--------- accessor -----------------------------------------------------

		ParticleSystemSettings& GetDraft() { return draft_; }
		const ParticleSystemSettings& GetDraft() const { return draft_; }
		ParticleSettingsEditState& GetEditorState() { return editorState_; }
	private:
		//========================================================================
		// private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleSystemSettings draft_;
		ParticleSettingsEditState editorState_;
	};
}
