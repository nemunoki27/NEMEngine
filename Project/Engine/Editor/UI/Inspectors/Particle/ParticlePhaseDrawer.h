#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleSettingsEditSession.h"
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

namespace Engine {

	//============================================================================
	//	ParticlePhaseDrawer class
	//	Phase一覧とModule編集を表示する
	//============================================================================
	class ParticlePhaseDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticlePhaseDrawer(ParticleSettingsEditSession& session, std::string& status, TextSearchFilter& search);
		// 編集結果を呼出元のsessionへ返す
		bool Draw(const EditorPanelContext& context, ParticleSystemSettings& group,
			ParticleSettingsEditState& editorState, ParticleSimulationTarget simulationTarget);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleSettingsEditSession& session_;
		std::string& statusMessage_;
		TextSearchFilter& addModuleSearchFilter_;

		//--------- functions ----------------------------------------------------

		// PhaseのMaterial入力を表示する
		bool DrawPhaseMaterialSection(const EditorPanelContext& context, ParticleSystemSettings& group, ParticlePhaseSettings& phase);
		// 選択Moduleの編集用instanceを表示する
		bool DrawPhaseModules(const EditorPanelContext& context, ParticleSystemSettings& group,
			ParticleSettingsEditState& editorState, ParticlePhaseSettings& phase, ParticleSimulationTarget simulationTarget);
		// 親への追従設定を表示する
		bool DrawPhaseParentSection(ParticleSystemSettings& group, ParticlePhaseSettings& phase, int32_t selectedPhase);
	};
}
