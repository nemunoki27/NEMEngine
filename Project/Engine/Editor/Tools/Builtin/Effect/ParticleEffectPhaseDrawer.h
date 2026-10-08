#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleEffectEditSession.h"
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

namespace Engine {

	//============================================================================
	//	ParticleEffectPhaseDrawer class
	//	Phase一覧とModule編集を表示する
	//============================================================================
	class ParticleEffectPhaseDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleEffectPhaseDrawer(ParticleEffectEditSession& session, std::string& status, TextSearchFilter& search);
		// 編集結果を呼出元のsessionへ返す
		bool Draw(const EditorToolContext& context, ParticleEffectGroup& group, ParticleGroupEditState& editorState);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleEffectEditSession& session_;
		std::string& statusMessage_;
		TextSearchFilter& addModuleSearchFilter_;

		//--------- functions ----------------------------------------------------

		// PhaseのMaterial入力を表示する
		bool DrawPhaseMaterialSection(const EditorToolContext& context, ParticleEffectGroup& group, ParticleEffectPhase& phase);
		// 選択Moduleの編集用instanceを表示する
		bool DrawPhaseModules(const EditorToolContext& context, ParticleEffectGroup& group,
			ParticleGroupEditState& editorState, ParticleEffectPhase& phase);
		// 親への追従設定を表示する
		bool DrawPhaseParentSection(ParticleEffectGroup& group, ParticleEffectPhase& phase, int32_t selectedPhase);
	};
}
