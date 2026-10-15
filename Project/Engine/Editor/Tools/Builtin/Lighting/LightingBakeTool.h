#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Rendering/Lighting/Baked/BakedLightingTypes.h>

namespace Engine {

	//============================================================================
	//	LightingBakeTool class
	//	Active Sceneの保存済み照明をベイクする
	//============================================================================
	class LightingBakeTool : public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LightingBakeTool() = default;
		~LightingBakeTool() override = default;
		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{.id = "engine.lighting_bake", .name = "照明ベイク", .category = "レンダリング",
			.owner = ToolOwner::Engine, .flags = ToolFlags::EditOnly, .order = 20};
		bool openWindow_ = false;
		int32_t atlasSize_ = 256;
		int32_t sampleCount_ = 128;
		int32_t bounceCount_ = 4;
		int32_t captureSize_ = 32;
		int32_t workBudget_ = 4096;
		float probeSpacing_ = 4.0f;
		float normalBias_ = 0.01f;
		AssetID sourceScene_{};
		std::filesystem::path scenePath_;
		std::string sceneRevision_;
		std::string status_;
		std::shared_ptr<const BakedLightingData> published_;

		//--------- functions ----------------------------------------------------

		// 保存済みWorldから入力を取得する
		void StartBake(const EditorToolContext& context);
		// 完了結果をSceneへ接続する
		void Publish(const EditorToolContext& context);
	};
}
