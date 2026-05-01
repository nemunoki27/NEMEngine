#include "MyGUI.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

//============================================================================
//	MyGUI classMethods
//============================================================================

namespace {

	//========================================================================
	//	レイアウト定数
	//========================================================================

	// 左側に表示する文字の幅
	constexpr float kLabelColumnWidth = 160.0f;
	constexpr float kAxisLabelWidth = 14.0f;

	//========================================================================
	//	軸情報
	//========================================================================

	struct AxisDisplayInfo {

		const char* name;
		ImVec4 color;
	};
	// 軸に対応する表示情報を取得する
	AxisDisplayInfo GetAxisDisplayInfo(char axis) {

		switch (axis) {
		case 'X': return { "X", ImVec4(1.0f, 0.26f, 0.20f, 1.0f) }; // red
		case 'Y': return { "Y", ImVec4(0.20f, 0.45f, 1.0f, 1.0f) }; // blue
		case 'Z': return { "Z", ImVec4(0.20f, 1.0f, 0.25f, 1.0f) }; // green
		case 'W': return { "W", ImVec4(0.90f, 0.78f, 0.20f, 1.0f) }; // yellow
		default:  return { "-", ImVec4(0.70f, 0.70f, 0.70f, 1.0f) };
		}
	}

	//========================================================================
	//	文字列ヘルパー
	//========================================================================

	// 精度を指定してfloatを文字列に変換する
	std::string FormatFloat(float value, uint32_t precision) {

		return std::format("{:.{}f}", value, precision);
	}
	// アセットのファイルパスから表示名を生成する
	std::string MakeAssetDisplayNameFromPath(const std::string& assetPath) {

		const std::filesystem::path path(assetPath);
		const std::string fileName = path.filename().string();
		const std::string lower = Engine::Algorithm::ToLower(fileName);

		if (Engine::Algorithm::EndsWith(lower, ".scene.json")) {
			return fileName.substr(0, fileName.size() - 5);
		}
		if (Engine::Algorithm::EndsWith(lower, ".prefab.json")) {
			return fileName.substr(0, fileName.size() - 5);
		}
		if (Engine::Algorithm::EndsWith(lower, ".material.json")) {
			return fileName.substr(0, fileName.size() - 5);
		}
		if (Engine::Algorithm::EndsWith(lower, ".shader.json")) {
			return fileName.substr(0, fileName.size() - 5);
		}
		if (Engine::Algorithm::EndsWith(lower, ".pipeline.json")) {
			return fileName.substr(0, fileName.size() - 5);
		}
		if (Engine::Algorithm::EndsWith(lower, ".graph.json")) {
			return fileName.substr(0, fileName.size() - 5);
		}
		return fileName;
	}
	// ドロップされたアセットの種類が受け入れ可能な種類のリストに含まれているかどうかを判定する
	bool IsAcceptedAssetType(Engine::AssetType type,
		const std::initializer_list<Engine::AssetType>& acceptedTypes) {

		if (acceptedTypes.size() == 0) {
			return true;
		}
		return std::find(acceptedTypes.begin(), acceptedTypes.end(), type) != acceptedTypes.end();
	}
	// ドロップされたペイロードがアセットのペイロードとして正しいかどうかを判定し、正しければペイロードを読み取る
	bool TryReadAssetPayload(const ImGuiPayload* payload, Engine::EditorAssetDragDropPayload& outPayload) {

		if (!payload || payload->DataSize != sizeof(Engine::EditorAssetDragDropPayload)) {
			return false;
		}

		outPayload = *static_cast<const Engine::EditorAssetDragDropPayload*>(payload->Data);
		return true;
	}
	// アセット参照のラベルテキストを構築する
	std::string BuildAssetReferenceLabel(Engine::AssetID assetID, const Engine::AssetDatabase* assetDatabase) {

		if (!assetID) {
			return "None (Drop asset here)";
		}

		if (!assetDatabase) {
			return std::format("GUID: {}", Engine::ToString(assetID));
		}

		const Engine::AssetMeta* meta = assetDatabase->Find(assetID);
		if (!meta) {
			return std::format("Missing Asset | GUID: {}", Engine::ToString(assetID));
		}

		const std::string displayName = MakeAssetDisplayNameFromPath(meta->assetPath);
		return std::format("Name: {}", displayName);
	}
	// アセット参照のツールチップテキストを構築する
	std::string BuildAssetReferenceTooltip(Engine::AssetID assetID, const Engine::AssetDatabase* assetDatabase) {

		if (!assetID) {
			return "Drop asset here";
		}

		if (!assetDatabase) {
			return std::format("GUID: {}", Engine::ToString(assetID));
		}

		const Engine::AssetMeta* meta = assetDatabase->Find(assetID);
		if (!meta) {
			return std::format("Missing Asset\nGUID: {}", Engine::ToString(assetID));
		}

		const std::string displayName = MakeAssetDisplayNameFromPath(meta->assetPath);
		return std::format(
			"Name : {}\nPath : {}\nGUID : {}",
			displayName,
			meta->assetPath,
			Engine::ToString(assetID));
	}

	//========================================================================
	//	レイアウトヘルパー
	//========================================================================

	// フィールドの幅を計算する
	float CalcFieldWidth(int fieldCount, float reserveRightWidth = 0.0f) {

		const float avail = (std::max)(1.0f, ImGui::GetContentRegionAvail().x - reserveRightWidth);
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float fieldArea = (std::max)(1.0f, avail - spacing * static_cast<float>((std::max)(0, fieldCount - 1)));
		return fieldArea / static_cast<float>(fieldCount);
	}

	//========================================================================
	//	描画ヘルパー
	//========================================================================

	// 軸ラベルを描画する
	void DrawAxisLabel(char axis) {

		const AxisDisplayInfo info = GetAxisDisplayInfo(axis);
		ImGui::PushStyleColor(ImGuiCol_Text, info.color);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(info.name);
		ImGui::PopStyleColor();
	}
	// コピー可能な値ボックスを描画する
	void DrawCopyableValueBox(const char* id, char axis, const std::string& valueText, float width) {

		ImGui::PushID(id);

		ImGui::BeginGroup();

		// 軸ラベル
		DrawAxisLabel(axis);
		ImGui::SameLine(0.0f, 6.0f);

		const ImVec2 buttonSize(width - kAxisLabelWidth - 6.0f, ImGui::GetFrameHeight());
		if (ImGui::Button(valueText.c_str(), buttonSize)) {
			ImGui::SetClipboardText(valueText.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Click to copy");
		}
		ImGui::EndGroup();
		ImGui::PopID();
	}
	// ドラッグ可能な値フィールドを描画する
	bool DrawDragFloatField(const char* id, char axis, float& value, float width, const Engine::FloatEditSetting& setting) {

		bool changed = false;

		ImGui::PushID(id);
		ImGui::BeginGroup();

		// 軸ラベル
		DrawAxisLabel(axis);
		ImGui::SameLine(0.0f, 6.0f);

		ImGui::SetNextItemWidth(width - kAxisLabelWidth - 6.0f);
		changed = ImGui::DragFloat("##Value", &value, setting.dragSpeed, setting.minValue, setting.maxValue, "%.3f");

		ImGui::EndGroup();
		ImGui::PopID();
		return changed;
	}
	// 複数のドラッグ可能な値フィールドを描画する
	Engine::ValueEditResult  DrawDragFields(const char* label, const std::array<char, 4>& axes,
		float* values, int count, const Engine::FloatEditSetting& setting) {

		Engine::ValueEditResult result{};

		const std::string tableID = std::string("##MyGUI_RowTable_Public_") + label;
		if (!ImGui::BeginTable(tableID.c_str(), 2,
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_BordersInnerV |
			ImGuiTableFlags_NoSavedSettings)) {
			return result;
		}
		ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, kLabelColumnWidth);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex(0);
		ImGui::PushID(label);
		const ImVec2 labelPos = ImGui::GetCursorScreenPos();
		const ImVec2 labelSize(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
		ImGui::InvisibleButton("##LabelDragAll", labelSize);

		// ラベル部分を左右にドラッグした場合は、全ての値を同じ量だけ動かす
		if (ImGui::IsItemActive()) {

			const float delta = ImGui::GetIO().MouseDelta.x * setting.dragSpeed;
			if (delta != 0.0f) {
				for (int i = 0; i < count; ++i) {
					values[i] = (std::clamp)(values[i] + delta, setting.minValue, setting.maxValue);
				}
				result.valueChanged = true;
			}
		}
		result.anyItemActive |= ImGui::IsItemActive();
		result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
		if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
			ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
		}

		const ImVec2 textPos(
			labelPos.x,
			labelPos.y + (labelSize.y - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::GetWindowDrawList()->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), label);

		ImGui::TableSetColumnIndex(1);
		const float fieldWidth = CalcFieldWidth(count, setting.reserveRightWidth);
		for (int i = 0; i < count; ++i) {
			if (i > 0) {
				ImGui::SameLine();
			}
			result.valueChanged |= DrawDragFloatField(axes[i] == '\0' ? "Value" : axes[i] == 'W' ? "W" :
				axes[i] == 'Z' ? "Z" : axes[i] == 'Y' ? "Y" : "X", axes[i],
				values[i], fieldWidth, setting);
			result.anyItemActive |= ImGui::IsItemActive();
			result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
		}
		// プロパティ行を閉じる
		if (setting.closeOnProperty) {
			Engine::MyGUI::EndPropertyRow();
		}
		return result;
	}
	// 複数のコピー可能な値フィールドを描画する
	void DrawTextFields(const char* label, const std::array<char, 4>& axes,
		const float* values, int count, uint32_t precision) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return;
		}

		const float fieldWidth = CalcFieldWidth(count);
		for (int i = 0; i < count; ++i) {

			if (i > 0) {
				ImGui::SameLine();
			}
			const std::string text = FormatFloat(values[i], precision);
			const std::string id = std::format("{}_{}", label, i);
			DrawCopyableValueBox(id.c_str(), axes[i], text, fieldWidth);
		}
		Engine::MyGUI::EndPropertyRow();
	}
	// 単一のコピー可能な値フィールドを描画する
	void DrawScalarTextField(const char* label, float value, uint32_t precision) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return;
		}

		const std::string text = FormatFloat(value, precision);
		if (ImGui::Button(text.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {
			ImGui::SetClipboardText(text.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Click to copy");
		}
		Engine::MyGUI::EndPropertyRow();
	}
	// Engine::SceneViewManipulatorModeをImGuizmo::OPERATIONに変換する
	ImGuizmo::OPERATION ToImGuizmoOperation(Engine::SceneViewManipulatorMode mode) {

		switch (mode) {
		case Engine::SceneViewManipulatorMode::Translate: return ImGuizmo::TRANSLATE;
		case Engine::SceneViewManipulatorMode::Rotate:    return ImGuizmo::ROTATE;
		case Engine::SceneViewManipulatorMode::Scale:     return ImGuizmo::SCALE;
		case Engine::SceneViewManipulatorMode::None:
		default:
			break;
		}
		return static_cast<ImGuizmo::OPERATION>(0);
	}
	Engine::Quaternion QuaternionFromRotationMatrixRowVector(const Engine::Matrix4x4& rowVectorMatrix) {

		// row-vector行列なので、標準的なcolumn-vector変換式を使うために転置して読む
		const float m00 = rowVectorMatrix.m[0][0];
		const float m01 = rowVectorMatrix.m[1][0];
		const float m02 = rowVectorMatrix.m[2][0];

		const float m10 = rowVectorMatrix.m[0][1];
		const float m11 = rowVectorMatrix.m[1][1];
		const float m12 = rowVectorMatrix.m[2][1];

		const float m20 = rowVectorMatrix.m[0][2];
		const float m21 = rowVectorMatrix.m[1][2];
		const float m22 = rowVectorMatrix.m[2][2];

		Engine::Quaternion q{};

		const float trace = m00 + m11 + m22;
		if (0.0f < trace) {

			const float s = std::sqrt(trace + 1.0f) * 2.0f;
			q.w = 0.25f * s;
			q.x = (m21 - m12) / s;
			q.y = (m02 - m20) / s;
			q.z = (m10 - m01) / s;
		} else if (m00 > m11 && m00 > m22) {

			const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
			q.w = (m21 - m12) / s;
			q.x = 0.25f * s;
			q.y = (m01 + m10) / s;
			q.z = (m02 + m20) / s;
		} else if (m11 > m22) {

			const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
			q.w = (m02 - m20) / s;
			q.x = (m01 + m10) / s;
			q.y = 0.25f * s;
			q.z = (m12 + m21) / s;
		} else {

			const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
			q.w = (m10 - m01) / s;
			q.x = (m02 + m20) / s;
			q.y = (m12 + m21) / s;
			q.z = 0.25f * s;
		}
		return Engine::Quaternion::Normalize(q);
	}
	// 3Dアフィン行列を平行移動、回転、拡縮に分解する
	bool DecomposeAffine3D(const Engine::Matrix4x4& matrix, Engine::Vector3& outPos, Engine::Quaternion& outRotation, Engine::Vector3& outScale) {

		constexpr float kEps = 1e-6f;

		outPos = matrix.GetTranslationValue();

		Engine::Vector3 axisX(matrix.m[0][0], matrix.m[0][1], matrix.m[0][2]);
		Engine::Vector3 axisY(matrix.m[1][0], matrix.m[1][1], matrix.m[1][2]);
		Engine::Vector3 axisZ(matrix.m[2][0], matrix.m[2][1], matrix.m[2][2]);

		outScale.x = axisX.Length();
		outScale.y = axisY.Length();
		outScale.z = axisZ.Length();

		if (outScale.x <= kEps || outScale.y <= kEps || outScale.z <= kEps) {
			return false;
		}

		axisX /= outScale.x;
		axisY /= outScale.y;
		axisZ /= outScale.z;

		// 負スケール補正
		const float handedness = Engine::Vector3::Dot(Engine::Vector3::Cross(axisX, axisY), axisZ);
		if (handedness < 0.0f) {
			outScale.z = -outScale.z;
			axisZ = -axisZ;
		}

		Engine::Matrix4x4 rotationMatrix = Engine::Matrix4x4::Identity();
		rotationMatrix.m[0][0] = axisX.x; rotationMatrix.m[0][1] = axisX.y; rotationMatrix.m[0][2] = axisX.z;
		rotationMatrix.m[1][0] = axisY.x; rotationMatrix.m[1][1] = axisY.y; rotationMatrix.m[1][2] = axisY.z;
		rotationMatrix.m[2][0] = axisZ.x; rotationMatrix.m[2][1] = axisZ.y; rotationMatrix.m[2][2] = axisZ.z;

		outRotation = QuaternionFromRotationMatrixRowVector(rotationMatrix);
		return true;
	}
	// 2Dアフィン行列を平行移動、回転（Z軸のみ）、拡縮に分解する
	bool DecomposeAffine2D(const Engine::Matrix4x4& matrix, Engine::Vector3& outPos, float& outRotationZ, Engine::Vector3& outScale) {

		constexpr float kEps = 1e-6f;

		outPos = matrix.GetTranslationValue();
		if (!std::isfinite(outPos.x) || !std::isfinite(outPos.y) || !std::isfinite(outPos.z)) {
			return false;
		}

		Engine::Vector2 axisX(matrix.m[0][0], matrix.m[0][1]);
		Engine::Vector2 axisY(matrix.m[1][0], matrix.m[1][1]);

		outScale.x = axisX.Length();
		outScale.y = axisY.Length();
		outScale.z = 1.0f;

		if (!std::isfinite(outScale.x) || !std::isfinite(outScale.y)) {
			return false;
		}
		if (outScale.x <= kEps || outScale.y <= kEps) {
			return false;
		}

		axisX /= outScale.x;
		axisY /= outScale.y;

		const float det = axisX.x * axisY.y - axisX.y * axisY.x;
		if (!std::isfinite(det)) {
			return false;
		}

		if (det < 0.0f) {

			outScale.y = -outScale.y;
			axisY = -axisY;
		}

		outRotationZ = Math::RadToDeg(std::atan2(axisX.y, axisX.x));
		if (!std::isfinite(outRotationZ)) {
			return false;
		}

		return true;
	}
	// 2D用にQuaternionからZ回転だけを安全に取り出す
	float ExtractRotationZDegrees2D(const Engine::Quaternion& rotation) {

		constexpr float kEps = 1e-6f;

		Engine::Quaternion normalized = Engine::Quaternion::Normalize(rotation);
		Engine::Matrix4x4 rotateMatrix = Engine::Quaternion::MakeRotateMatrix(normalized);

		// row-vector前提で、ローカルX軸のXY成分からZ回転を求める
		Engine::Vector2 axisX(rotateMatrix.m[0][0], rotateMatrix.m[0][1]);
		const float len = axisX.Length();
		if (len <= kEps || !std::isfinite(len)) {
			return 0.0f;
		}

		axisX /= len;

		const float angle = std::atan2(axisX.y, axisX.x);
		if (!std::isfinite(angle)) {
			return 0.0f;
		}

		return Math::RadToDeg(angle);
	}
	// ギズモ操作を開始する
	void BeginGizmoManipulate([[maybe_unused]] const char* id, const Engine::GizmoViewContext& context, bool is2DTarget) {

		//ImGuizmo::PushID(id);
		ImGuizmo::SetDrawlist();
		ImGuizmo::SetRect(context.rect.x, context.rect.y, context.rect.width, context.rect.height);
		ImGuizmo::SetOrthographic(context.orthographic);
		ImGuizmo::AllowAxisFlip(context.allowAxisFlip);

		if (is2DTarget) {
			//ImGuizmo::SetAxisMask(false, false, true);
		} else {
			//ImGuizmo::SetAxisMask(false, false, false);
		}
	}
	// ギズモ操作を終了する
	void EndGizmoManipulate() {

		//ImGuizmo::PopID();
	}
}

bool Engine::MyGUI::CollapsingHeader(const char* label, bool stratOpen) {

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

	ImGuiTreeNodeFlags flags = {};
	if (stratOpen) {

		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	}
	bool open = ImGui::CollapsingHeader(label, flags);

	ImGui::PopStyleVar(3);

	return open;
}

Engine::TextInputPopupResult Engine::MyGUI::InputTextPopupContent(const char* label, std::string& text, const char* errorText) {

	TextInputPopupResult result{};

	ImGui::TextUnformatted(label);
	ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

	const bool submittedByEnter = ImGui::InputText("##InputTextPopupValue", &text, ImGuiInputTextFlags_EnterReturnsTrue);
	if (ImGui::IsWindowAppearing()) {
		ImGui::SetKeyboardFocusHere(-1);
	}

	if (errorText && errorText[0] != '\0') {
		ImGui::TextColored(ImVec4(0.95f, 0.32f, 0.24f, 1.0f), "%s", errorText);
	} else {
		ImGui::Spacing();
	}

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(96.0f, 0.0f)) || submittedByEnter) {
		result.submitted = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
		result.canceled = true;
	}
	return result;
}

bool Engine::MyGUI::BeginPropertyRow(const char* label) {

	const std::string tableID = std::string("##MyGUI_RowTable_Public_") + label;
	if (!ImGui::BeginTable(tableID.c_str(), 2,
		ImGuiTableFlags_SizingStretchProp |
		ImGuiTableFlags_BordersInnerV |
		ImGuiTableFlags_NoSavedSettings)) {
		return false;
	}

	ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, kLabelColumnWidth);
	ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

	ImGui::TableNextRow();

	ImGui::TableSetColumnIndex(0);
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(label);

	ImGui::TableSetColumnIndex(1);
	ImGui::PushID(label);
	return true;
}

void Engine::MyGUI::EndPropertyRow() {

	ImGui::PopID();
	ImGui::EndTable();
}

void Engine::MyGUI::TextFloat(const char* label, float value, uint32_t precision) {

	DrawScalarTextField(label, value, precision);
}

void Engine::MyGUI::TextVector2(const char* label, const Vector2& value, uint32_t precision) {

	const float values[2] = { value.x, value.y };
	DrawTextFields(label, { 'X', 'Y', '\0', '\0' }, values, 2, precision);
}

void Engine::MyGUI::TextVector3(const char* label, const Vector3& value, uint32_t precision) {

	const float values[3] = { value.x, value.y, value.z };
	DrawTextFields(label, { 'X', 'Y', 'Z', '\0' }, values, 3, precision);
}

void Engine::MyGUI::TextQuaternion(const char* label, const Quaternion& value, uint32_t precision) {

	const float values[4] = { value.x, value.y, value.z, value.w };
	DrawTextFields(label, { 'X', 'Y', 'Z', 'W' }, values, 4, precision);
}

void Engine::MyGUI::TextMatrix4x4(const char* label, const Matrix4x4& value, uint32_t precision) {

	if (!BeginPropertyRow(label)) {
		return;
	}

	const std::string tableID = std::string("##MyGUI_Matrix_") + label;
	if (ImGui::BeginTable(tableID.c_str(), 4, ImGuiTableFlags_Borders |
		ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
		for (int row = 0; row < 4; ++row) {

			ImGui::TableNextRow();
			for (int col = 0; col < 4; ++col) {

				ImGui::TableSetColumnIndex(col);

				const std::string text = FormatFloat(value.m[row][col], precision);
				const std::string buttonID = std::format("##{}_{}_{}", label, row, col);
				if (ImGui::Button((text + buttonID).c_str(), ImVec2(-FLT_MIN, 0.0f))) {
					ImGui::SetClipboardText(text.c_str());
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Click to copy");
				}
			}
		}
		ImGui::EndTable();
	}
	EndPropertyRow();
}

Engine::ValueEditResult Engine::MyGUI::DragFloat(const char* label, float& value, const FloatEditSetting& setting) {

	float values[1] = { value };
	ValueEditResult result = DrawDragFields(label, { 'X', '\0', '\0', '\0' }, values, 1, setting);
	if (result.valueChanged) {
		value = values[0];
	}
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragInt(const char* label, int32_t& value, const IntEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	int32_t v = value;
	result.valueChanged = ImGui::DragInt("##Value", &v, setting.dragSpeed, setting.minValue, setting.maxValue);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit();

	if (result.valueChanged) {
		value = v;
	}

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragVector2(const char* label, Vector2& value, const FloatEditSetting& setting) {

	float values[2] = { value.x, value.y };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', '\0', '\0' }, values, 2, setting);
	if (result.valueChanged) {
		value.x = values[0];
		value.y = values[1];
	}
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragVector3(const char* label, Vector3& value, const FloatEditSetting& setting) {

	float values[3] = { value.x, value.y, value.z };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', 'Z', '\0' }, values, 3, setting);
	if (result.valueChanged) {
		value.x = values[0];
		value.y = values[1];
		value.z = values[2];
	}
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragVector4(const char* label, Vector4& value, const FloatEditSetting& setting) {

	float values[4] = { value.x, value.y, value.z, value.w };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', 'Z', 'W' }, values, 4, setting);
	if (result.valueChanged) {
		value.x = values[0];
		value.y = values[1];
		value.z = values[2];
		value.w = values[3];
	}
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragQuaternion(const char* label, Quaternion& value, bool displayEuler, const FloatEditSetting& setting) {

	float values[4] = { value.x, value.y, value.z, value.w };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', 'Z', 'W' }, values, 4, setting);
	if (result.valueChanged) {
		value.x = values[0];
		value.y = values[1];
		value.z = values[2];
		value.w = values[3];
		value = Quaternion::Normalize(value);
	}
	if (displayEuler) {

		ImGui::Separator();

		// オイラーを下に表示する
		Vector3 euler = Quaternion::ToEulerAngles(value);
		const float eulerValues[3] = { euler.x, euler.y, euler.z };
		DrawTextFields((std::string(label) + " (Euler Deg)").c_str(), { 'X', 'Y', 'Z', '\0' }, eulerValues, 3, 3);
	}
	return result;
}

Engine::ValueEditResult Engine::MyGUI::ColorEdit(const char* label, Color3& value) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	float color[3] = { value.r, value.g, value.b };
	result.valueChanged = ImGui::ColorEdit3("##Value", color, ImGuiColorEditFlags_Float);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit() || result.valueChanged;

	if (result.valueChanged) {
		value.r = color[0];
		value.g = color[1];
		value.b = color[2];
	}

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::ColorEdit(const char* label, Color4& value) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	float color[4] = { value.r, value.g, value.b, value.a };
	result.valueChanged = ImGui::ColorEdit4("##Value", color, ImGuiColorEditFlags_Float);
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = ImGui::IsItemDeactivatedAfterEdit() || result.valueChanged;

	if (result.valueChanged) {
		value.r = color[0];
		value.g = color[1];
		value.b = color[2];
		value.a = color[3];
	}

	EndPropertyRow();
	return result;
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate2D(const char* id,
	const GizmoViewContext& context, TransformComponent& transform) {

	GizmoEditResult result{};

	// ギズモ操作が有効で、かつ有効な描画領域がある場合にのみ操作を行う
	if (context.mode == SceneViewManipulatorMode::None || !context.rect.IsValid()) {
		return result;
	}

	// 操作モードをImGuizmoの形式に変換する。無効なモードの場合は操作を行わない
	ImGuizmo::OPERATION operation = ToImGuizmoOperation(context.mode);
	if (operation == static_cast<ImGuizmo::OPERATION>(0)) {
		return result;
	}

	// 2DギズモはZ軸回転のみを扱うため、現在のローカル回転からZ軸回転を抽出して、他の回転成分を打ち消した行列を作る
	float currentRotationZ = ExtractRotationZDegrees2D(transform.localRotation);
	TransformComponent planeTransform = transform;
	planeTransform.localRotation = Quaternion::Normalize(Quaternion::FromEulerDegrees(Vector3(0.0f, 0.0f, currentRotationZ)));
	planeTransform.localScale.z = 1.0f;

	// ローカルSRTからワールド行列を計算する
	Matrix4x4 localMatrix = Matrix4x4::MakeAffineMatrix(planeTransform.localScale, planeTransform.localRotation, planeTransform.localPos);
	Matrix4x4 worldMatrix = localMatrix * context.parentWorldMatrix;

	// ImGuizmoは行列をfloat[16]の形式で受け取るので変換する
	float view[16]{};
	float projection[16]{};
	float matrix[16]{};
	Math::MatrixToFloat16(context.viewMatrix, view);
	Math::MatrixToFloat16(context.projectionMatrix, projection);
	Math::MatrixToFloat16(worldMatrix, matrix);

	// ギズモ操作を開始する
	BeginGizmoManipulate(id, context, true);

	result.valueChanged = ImGuizmo::Manipulate(view, projection, operation, ImGuizmo::LOCAL, matrix);
	result.isOver = ImGuizmo::IsOver();
	result.isUsing = ImGuizmo::IsUsing();

	// ギズモ操作を終了する
	EndGizmoManipulate();

	// 値が変更されていない場合はこれ以上の処理は不要
	if (!result.valueChanged) {
		return result;
	}

	// 編集された行列をMatrix4x4に変換し、親のワールド行列の逆行列を掛けてローカル行列に変換する
	Matrix4x4 editedWorld = Math::MatrixFromFloat16(matrix);
	Matrix4x4 editedLocal = editedWorld * Matrix4x4::Inverse(context.parentWorldMatrix);

	Vector3 pos{};
	Vector3 scale{};
	float rotationZ = 0.0f;
	if (!DecomposeAffine2D(editedLocal, pos, rotationZ, scale)) {
		result.valueChanged = false;
		return result;
	}

	// 2DギズモはZ軸回転のみを扱うため、編集されたZ軸回転を現在のZ軸回転に連続させる
	float nextRotationZ = Math::MakeContinuousAngleDegrees(rotationZ, currentRotationZ);

	// 編集結果を反映
	transform.localPos.x = pos.x;
	transform.localPos.y = pos.y;
	transform.localPos.z = pos.z;
	transform.localRotation = Quaternion::Normalize(Quaternion::FromEulerDegrees(Vector3(0.0f, 0.0f, nextRotationZ)));
	transform.localScale.x = scale.x;
	transform.localScale.y = scale.y;
	transform.isDirty = true;
	return result;
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate3D(const char* id,
	const GizmoViewContext& context, TransformComponent& transform) {

	GizmoEditResult result{};

	// ギズモ操作が有効で、かつ有効な描画領域がある場合にのみ操作を行う
	if (context.mode == SceneViewManipulatorMode::None || !context.rect.IsValid()) {
		return result;
	}

	// 操作モードをImGuizmoの形式に変換する。無効なモードの場合は操作を行わない
	ImGuizmo::OPERATION operation = ToImGuizmoOperation(context.mode);
	if (operation == static_cast<ImGuizmo::OPERATION>(0)) {
		return result;
	}

	// ローカルSRTからワールド行列を計算する
	Matrix4x4 localMatrix = Matrix4x4::MakeAffineMatrix(transform.localScale, transform.localRotation, transform.localPos);
	Matrix4x4 worldMatrix = localMatrix * context.parentWorldMatrix;

	// ImGuizmoは行列をfloat[16]の形式で受け取るので変換する
	float view[16]{};
	float projection[16]{};
	float matrix[16]{};
	Math::MatrixToFloat16(context.viewMatrix, view);
	Math::MatrixToFloat16(context.projectionMatrix, projection);
	Math::MatrixToFloat16(worldMatrix, matrix);

	// ギズモ操作を開始する
	BeginGizmoManipulate(id, context, false);

	result.valueChanged = ImGuizmo::Manipulate(view, projection, operation, ImGuizmo::LOCAL, matrix);
	result.isOver = ImGuizmo::IsOver();
	result.isUsing = ImGuizmo::IsUsing();

	// ギズモ操作を終了する
	EndGizmoManipulate();

	// 値が変更されていない場合はこれ以上の処理は不要
	if (!result.valueChanged) {
		return result;
	}

	// 編集された行列をMatrix4x4に変換し、親のワールド行列の逆行列を掛けてローカル行列に変換する
	Matrix4x4 editedWorld = Math::MatrixFromFloat16(matrix);
	Matrix4x4 editedLocal = editedWorld * Matrix4x4::Inverse(context.parentWorldMatrix);

	Vector3 pos{};
	Vector3 scale{};
	Quaternion rotation{};
	if (!DecomposeAffine3D(editedLocal, pos, rotation, scale)) {
		result.valueChanged = false;
		return result;
	}
	// 編集結果を反映
	transform.localPos = pos;
	transform.localRotation = rotation;
	transform.localScale = scale;
	transform.isDirty = true;
	return result;
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate2D(const char* id,
	const GizmoViewContext& context, MeshSubMeshTextureOverride& subMesh) {

	GizmoEditResult result{};

	// ギズモ操作が有効で、かつ有効な描画領域がある場合にのみ操作を行う
	if (context.mode == SceneViewManipulatorMode::None || !context.rect.IsValid()) {
		return result;
	}

	// 操作モードをImGuizmoの形式に変換する。無効なモードの場合は操作を行わない
	ImGuizmo::OPERATION operation = ToImGuizmoOperation(context.mode);
	if (operation == static_cast<ImGuizmo::OPERATION>(0)) {
		return result;
	}

	// 2DギズモはZ軸回転のみを扱うため、現在のローカル回転からZ軸回転を抽出して、他の回転成分を打ち消した行列を作る
	MeshSubMeshTextureOverride planeSubMesh = subMesh;
	planeSubMesh.localRotation.x = 0.0f;
	planeSubMesh.localRotation.y = 0.0f;
	planeSubMesh.localScale.z = 1.0f;

	// ローカルSRTからワールド行列を計算する
	Matrix4x4 localMatrix = MeshSubMeshRuntime::BuildLocalMatrix(planeSubMesh);
	Matrix4x4 worldMatrix = localMatrix * context.parentWorldMatrix;

	// ImGuizmoは行列をfloat[16]の形式で受け取るので変換する
	float view[16]{};
	float projection[16]{};
	float matrix[16]{};
	Math::MatrixToFloat16(context.viewMatrix, view);
	Math::MatrixToFloat16(context.projectionMatrix, projection);
	Math::MatrixToFloat16(worldMatrix, matrix);

	// ギズモ操作を開始する
	BeginGizmoManipulate(id, context, true);

	result.valueChanged = ImGuizmo::Manipulate(view, projection, operation, ImGuizmo::LOCAL, matrix);
	result.isOver = ImGuizmo::IsOver();
	result.isUsing = ImGuizmo::IsUsing();

	// ギズモ操作を終了する
	EndGizmoManipulate();

	// 値が変更されていない場合はこれ以上の処理は不要
	if (!result.valueChanged) {
		return result;
	}

	// 編集された行列をMatrix4x4に変換し、親のワールド行列の逆行列を掛けてローカル行列に変換する
	Matrix4x4 editedWorld = Math::MatrixFromFloat16(matrix);
	Matrix4x4 editedLocal = editedWorld * Matrix4x4::Inverse(context.parentWorldMatrix);

	Vector3 pos{};
	Vector3 scale{};
	float rotationZ = 0.0f;
	if (!DecomposeAffine2D(editedLocal, pos, rotationZ, scale)) {
		result.valueChanged = false;
		return result;
	}

	// 編集結果を反映
	subMesh.localPos.x = pos.x;
	subMesh.localPos.y = pos.y;
	subMesh.localPos.z = pos.z;
	// 2DではXY回転を常に0に固定する
	subMesh.localRotation.x = 0.0f;
	subMesh.localRotation.y = 0.0f;
	subMesh.localRotation.z = Math::MakeContinuousAngleDegrees(rotationZ, subMesh.localRotation.z);
	subMesh.localScale.x = scale.x;
	subMesh.localScale.y = scale.y;
	return result;
}

Engine::GizmoEditResult Engine::MyGUI::Manipulate3D(const char* id,
	const GizmoViewContext& context, MeshSubMeshTextureOverride& subMesh) {

	GizmoEditResult result{};

	// ギズモ操作が有効で、かつ有効な描画領域がある場合にのみ操作を行う
	if (context.mode == SceneViewManipulatorMode::None || !context.rect.IsValid()) {
		return result;
	}

	// 操作モードをImGuizmoの形式に変換する。無効なモードの場合は操作を行わない
	ImGuizmo::OPERATION operation = ToImGuizmoOperation(context.mode);
	if (operation == static_cast<ImGuizmo::OPERATION>(0)) {
		return result;
	}

	// ローカルSRTからワールド行列を計算する
	Matrix4x4 pivotMatrix = Matrix4x4::MakeTranslateMatrix(subMesh.sourcePivot);
	Matrix4x4 worldMatrix = pivotMatrix * subMesh.worldMatrix;

	// ImGuizmoは行列をfloat[16]の形式で受け取るので変換する
	float view[16]{};
	float projection[16]{};
	float matrix[16]{};
	Math::MatrixToFloat16(context.viewMatrix, view);
	Math::MatrixToFloat16(context.projectionMatrix, projection);
	Math::MatrixToFloat16(worldMatrix, matrix);

	// ギズモ操作を開始する
	BeginGizmoManipulate(id, context, false);

	result.valueChanged = ImGuizmo::Manipulate(view, projection, operation, ImGuizmo::LOCAL, matrix);
	result.isOver = ImGuizmo::IsOver();
	result.isUsing = ImGuizmo::IsUsing();

	// ギズモ操作を終了する
	EndGizmoManipulate();

	// 値が変更されていない場合はこれ以上の処理は不要
	if (!result.valueChanged) {
		return result;
	}

	// 編集された行列をMatrix4x4に変換し、親のワールド行列の逆行列を掛けてローカル行列に変換する
	Matrix4x4 editedGizmoWorld = Math::MatrixFromFloat16(matrix);
	// サブメッシュのワールド行列はピボットを考慮した形になっているため、ギズモ操作前後でピボットの影響を打ち消す必要がある
	Matrix4x4 invPivot = Matrix4x4::MakeTranslateMatrix(Vector3(-subMesh.sourcePivot.x, -subMesh.sourcePivot.y, -subMesh.sourcePivot.z));
	Matrix4x4 pivot = Matrix4x4::MakeTranslateMatrix(subMesh.sourcePivot);
	// 編集されたワールド行列からピボットの影響を打ち消す
	Matrix4x4 editedRenderWorld = invPivot * editedGizmoWorld;
	Matrix4x4 editedRenderLocal = editedRenderWorld * Matrix4x4::Inverse(context.parentWorldMatrix);
	Matrix4x4 editedAuthoringLocal = pivot * editedRenderLocal * invPivot;

	Vector3 pos{};
	Vector3 scale{};
	Quaternion rotation{};
	if (!DecomposeAffine3D(editedAuthoringLocal, pos, rotation, scale)) {
		result.valueChanged = false;
		return result;
	}

	Vector3 rawEuler = Quaternion::ToEulerDegrees(rotation);

	// 編集結果を反映
	subMesh.localPos = pos;
	subMesh.localRotation = Vector3::MakeContinuousDegrees(rawEuler, subMesh.localRotation);
	subMesh.localScale = scale;
	return result;
}

bool Engine::MyGUI::Checkbox(const char* label, bool& value) {

	if (!BeginPropertyRow(label)) {
		return false;
	}

	bool changed = ImGui::Checkbox("##Value", &value);

	EndPropertyRow();
	return changed;
}

bool Engine::MyGUI::SmallCheckbox(const char* id, bool& value) {

	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 1.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

	const bool changed = ImGui::Checkbox(id, &value);

	ImGui::PopStyleVar(2);
	return changed;
}

Engine::ValueEditResult Engine::MyGUI::InputText(const char* label, std::string& text, const TextEditSetting& setting) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	bool submittedByEnter = false;
	if (setting.multiLine) {

		ImVec2 inputSize = setting.size;
		if (inputSize.x <= 0.0f) {
			inputSize.x = ImGui::GetContentRegionAvail().x;
		}
		if (inputSize.y <= 0.0f) {
			inputSize.y = ImGui::GetFrameHeightWithSpacing() * 4.0f;
		}
		ImGui::InputTextMultiline("##Value", &text, inputSize, setting.flags);
	} else {

		submittedByEnter = ImGui::InputText("##Value", &text, setting.flags | ImGuiInputTextFlags_EnterReturnsTrue);
	}

	result.valueChanged = ImGui::IsItemEdited();
	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = submittedByEnter || ImGui::IsItemDeactivatedAfterEdit();

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::StringCombo(const char* label, std::string& currentValue,
	std::span<const std::string> items, const char* emptyPreview, bool allowEmptySelection) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	const char* preview = currentValue.empty() ? emptyPreview : currentValue.c_str();
	if (items.empty()) {

		ImGui::TextDisabled("%s", emptyPreview);
	} else if (ImGui::BeginCombo("##Value", preview)) {
		// 空選択を許可する場合
		if (allowEmptySelection) {
			const bool selected = currentValue.empty();
			if (ImGui::Selectable(emptyPreview, selected)) {
				if (!currentValue.empty()) {
					currentValue.clear();
					result.valueChanged = true;
				}
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}

		for (const std::string& item : items) {

			const bool selected = (currentValue == item);
			if (ImGui::Selectable(item.c_str(), selected)) {
				if (currentValue != item) {
					currentValue = item;
					result.valueChanged = true;
				}
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}

	result.anyItemActive = ImGui::IsItemActive();
	result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();

	EndPropertyRow();
	return result;
}

Engine::ValueEditResult Engine::MyGUI::AssetReferenceField(const char* label, AssetID& value,
	const AssetDatabase* assetDatabase, const std::initializer_list<AssetType>& acceptedTypes) {

	ValueEditResult result{};

	if (!BeginPropertyRow(label)) {
		return result;
	}

	// 表示テキストを構築する
	const std::string displayText = BuildAssetReferenceLabel(value, assetDatabase);
	const bool hasValue = static_cast<bool>(value);
	// 値がない場合はテキストを薄く表示する
	if (!hasValue) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}

	// ドロップターゲットを描画する
	ImGui::Button(displayText.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));

	if (!hasValue) {
		ImGui::PopStyleColor();
	}

	// アイテムがアクティブかどうかを記録する
	result.anyItemActive = ImGui::IsItemActive();
	if (ImGui::BeginItemTooltip()) {

		const std::string tooltip = BuildAssetReferenceTooltip(value, assetDatabase);
		ImGui::TextUnformatted(tooltip.c_str());
		ImGui::EndTooltip();
	}

	// ドロップされたペイロードを受け入れる
	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType)) {
			if (payload->IsDelivery()) {

				EditorAssetDragDropPayload assetPayload{};
				if (TryReadAssetPayload(payload, assetPayload) && IsAcceptedAssetType(assetPayload.assetType, acceptedTypes)) {
					if (value != assetPayload.assetID) {
						value = assetPayload.assetID;
						result.valueChanged = true;
						result.editFinished = true;
					}
				}
			}
		}
		ImGui::EndDragDropTarget();
	}

	EndPropertyRow();
	return result;
}
