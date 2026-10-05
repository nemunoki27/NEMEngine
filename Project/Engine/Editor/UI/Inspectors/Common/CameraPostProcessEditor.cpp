#include "CameraPostProcessEditor.h"

//============================================================================
//	CameraPostProcessEditor classMethods
//============================================================================
Engine::ValueEditResult Engine::CameraPostProcessEditor::Draw(
	ColorPipelineSettings& settings) {

	ValueEditResult result{};
	const auto accumulate = [&](const ValueEditResult& field) {

		result.valueChanged |= field.valueChanged;
		result.anyItemActive |= field.anyItemActive;
		result.editFinished |= field.editFinished;
	};

	// 露出と明暗の順応を編集する
	accumulate(MyGUI::EnumCombo("露出モード", settings.exposure.mode));
	accumulate(MyGUI::DragFloat("EV100", settings.exposure.manualEV100));
	accumulate(MyGUI::DragFloat("露出補正", settings.exposure.compensation));
	const bool preExposureChanged = MyGUI::Checkbox("Pre-Exposure", settings.exposure.usePreExposure);
	result.valueChanged |= preExposureChanged;
	result.editFinished |= preExposureChanged;
	accumulate(MyGUI::DragFloat("最小EV100", settings.exposure.minEV100));
	accumulate(MyGUI::DragFloat("最大EV100", settings.exposure.maxEV100));
	accumulate(MyGUI::DragFloat("明順応速度", settings.exposure.speedUp));
	accumulate(MyGUI::DragFloat("暗順応速度", settings.exposure.speedDown));

	// フィルム特性と色補正を編集する
	accumulate(MyGUI::DragFloat("Slope", settings.filmic.slope));
	accumulate(MyGUI::DragFloat("Toe", settings.filmic.toe));
	accumulate(MyGUI::DragFloat("Shoulder", settings.filmic.shoulder));
	accumulate(MyGUI::DragFloat("Black Clip", settings.filmic.blackClip));
	accumulate(MyGUI::DragFloat("White Clip", settings.filmic.whiteClip));
	accumulate(MyGUI::ColorEdit("カラーフィルター", settings.colorGrading.colorFilter));
	accumulate(MyGUI::DragFloat("色温度", settings.colorGrading.temperature));
	accumulate(MyGUI::DragFloat("Tint", settings.colorGrading.tint));
	accumulate(MyGUI::DragVector3("彩度", settings.colorGrading.saturation));
	accumulate(MyGUI::DragVector3("コントラスト", settings.colorGrading.contrast));
	accumulate(MyGUI::DragVector3("ガンマ", settings.colorGrading.gamma));
	accumulate(MyGUI::DragVector3("ゲイン", settings.colorGrading.gain));
	accumulate(MyGUI::DragVector3("オフセット", settings.colorGrading.offset));
	return result;
}
