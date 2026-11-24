#pragma once

#include <Engine/Editor/UI/Panels/Builtin/InspectorMaterialPreview.h>

namespace Engine {
	class ShaderGraphEditSession;

	// グラフの最終出力をキャンバス右下に表示する
	class ShaderGraphPreviewController {
	public:
		explicit ShaderGraphPreviewController(ShaderGraphEditSession& session);
		void Draw(const EditorToolContext& context, ImVec2 origin, ImVec2 canvasSize);

	private:
		ShaderGraphEditSession& session_;
		InspectorMaterialPreview preview_;
		AssetID graph_{};
		uint64_t revision_ = 0;
		double deadline_ = 0.0;
		bool failed_ = false;
		bool animated_ = false;
	};
}
