#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleEffectEditSession.h"

namespace Engine {

	//============================================================================
	//	ParticleEffectGroupDrawer class
	//	Groupの発生・形状・描画設定を表示する
	//============================================================================
	class ParticleEffectGroupDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleEffectGroupDrawer(ParticleEffectEditSession& session, std::string& status);
		// 編集結果を呼出元のsessionへ返す
		bool Draw(const EditorToolContext& context, ParticleEffectGroup& group);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ParticleEffectEditSession& session_;
		std::string& statusMessage_;

		//--------- functions ----------------------------------------------------

		// Trail用MaterialのTexture入力を表示する
		bool DrawTrailMaterialSection(const EditorToolContext& context, ParticleEffectGroup& group);
	};
}
