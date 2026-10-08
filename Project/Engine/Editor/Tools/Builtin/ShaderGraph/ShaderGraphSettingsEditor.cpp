#include "ShaderGraphSettingsEditor.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"
#include "ShaderGraphPreviewController.h"
#include "ShaderGraphEnumWidget.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

using Engine::ShaderGraphWidgets::D3D12EnumCombo;

Engine::ShaderGraphSettingsEditor::ShaderGraphSettingsEditor(
	ShaderGraphEditSession& session, ShaderGraphPreviewController& preview)
	: editSession_(session), previewController_(preview) {
}

bool Engine::ShaderGraphSettingsEditor::Draw() {

	if (!MyGUI::CollapsingHeader("グラフ設定", true)) {
		return false;
	}

	// Graphの名前と精度を編集する
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphSettings");
	editSession_.MarkDirty(MyGUI::InputText("名前", editSession_.GetDraft().name).valueChanged);
	ImGui::Text("種類: %s", EnumAdapter<ShaderGraphDomain>::ToString(editSession_.GetDraft().domain));
	editSession_.MarkDirty(MyGUI::EnumCombo("既定精度", editSession_.GetDraft().defaultPrecision).valueChanged);
	if (editSession_.GetDraft().domain != ShaderGraphDomain::Surface) {
		return false;
	}

	bool targetChanged = false;
	ShaderGraphTarget target = editSession_.GetDraft().target;
	if (MyGUI::EnumCombo("描画対象", target).valueChanged) {

		// 元のMaterialを復元してから描画対象を切り替える
		previewController_.Restore();
		targetChanged = editSession_.ChangeTarget(target);
	}

	// 描画条件の変更を編集sessionへ通知する
	editSession_.MarkDirty(MyGUI::EnumCombo("サーフェス", editSession_.GetDraft().surfaceMode).valueChanged);
	editSession_.MarkDirty(MyGUI::EnumCombo("ブレンド", editSession_.GetDraft().renderState.blendMode).valueChanged);
	editSession_.MarkDirty(MyGUI::Checkbox("両面描画", editSession_.GetDraft().renderState.twoSided));
	editSession_.MarkDirty(D3D12EnumCombo("塗りモード", editSession_.GetDraft().renderState.fillMode).valueChanged);
	editSession_.MarkDirty(D3D12EnumCombo("カリング", editSession_.GetDraft().renderState.cullMode).valueChanged);
	editSession_.MarkDirty(MyGUI::Checkbox("前面反時計回り", editSession_.GetDraft().renderState.frontCounterClockwise));
	editSession_.MarkDirty(MyGUI::Checkbox("深度クリップ", editSession_.GetDraft().renderState.depthClipEnable));
	editSession_.MarkDirty(MyGUI::Checkbox("深度書き込み", editSession_.GetDraft().renderState.depthWrite));
	editSession_.MarkDirty(MyGUI::Checkbox("深度テスト", editSession_.GetDraft().renderState.depthTest));
	editSession_.MarkDirty(D3D12EnumCombo("深度比較", editSession_.GetDraft().renderState.depthFunc).valueChanged);
	editSession_.MarkDirty(MyGUI::Checkbox("ステンシル", editSession_.GetDraft().renderState.stencilEnable));
	editSession_.MarkDirty(MyGUI::Checkbox("アルファクリップ", editSession_.GetDraft().renderState.alphaClipping));
	if (IsShaderGraph3DTarget(editSession_.GetDraft().target)) {
		editSession_.MarkDirty(MyGUI::Checkbox("影を落とす", editSession_.GetDraft().renderState.castShadows));
		editSession_.MarkDirty(MyGUI::Checkbox("影を受ける", editSession_.GetDraft().renderState.receiveShadows));
	}
	return targetChanged;
}
