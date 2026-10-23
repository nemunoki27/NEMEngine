#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleSettingsEditSession.h"

namespace Engine {

	//============================================================================
	//	ParticleEmitterDrawer class
	//	Groupの発生・形状・描画設定を表示する
	//============================================================================
	class ParticleEmitterDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleEmitterDrawer(ParticleSettingsEditSession& session, std::string& status);
		// 編集結果を呼出元のsessionへ返す
		bool Draw(const EditorPanelContext& context, ParticleSystemSettings& group);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleSettingsEditSession& session_;
		std::string& statusMessage_;

		//--------- functions ----------------------------------------------------

		// Trail用MaterialのTexture入力を表示する
		bool DrawTrailMaterialSection(const EditorPanelContext& context, ParticleSystemSettings& group);
	};
}
