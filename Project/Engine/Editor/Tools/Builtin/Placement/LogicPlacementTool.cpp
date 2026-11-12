#include "LogicPlacementTool.h"
#include <Engine/Editor/Commands/Entity/PlaceObjectsCommand.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/LogicPlacementFields.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Utility/LogicPlacementPreview.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

#include <array>
#include <exception>

using namespace Engine;

LogicPlacementTool::LogicPlacementTool() {

	LoadSettings();
	preview_ = std::make_shared<LogicPlacementResult>(BuildLogicPlacement(request_.settings));
}

LogicPlacementTool::~LogicPlacementTool() {

	SaveSettings();
}

void LogicPlacementTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) return;
	auto* world = context.GetWorld();
	if (world && (observedWorld_.lock() != world->GetLifetime() || observedScene_ != context.toolContext.activeSceneInstanceID)) {
		request_.source.object = {};
		observedWorld_ = world->GetLifetime();
		observedScene_ = context.toolContext.activeSceneInstanceID;
		prefabCache_.Clear();
	}
	ImGui::SetNextWindowSize(ImVec2(480, 620), ImGuiCond_FirstUseEver);
	const bool expanded = EditorWindowPresentation::Begin(MakeWindowName("ロジック配置").c_str(), &openWindow_);
	if (expanded) {
		if (!world || !context.CanEditScene() || !context.panelContext || !context.panelContext->host) {
			MyGUI::Text("配置", "編集可能なシーンがありません");
		} else {
			MyGUI::ScopedPropertyLabelWidth labelWidth("LogicPlacementSettings");
			static const std::array<std::string, 2> dimensions{"2D", "3D"};
			std::string dimension = request_.settings.is2D ? "2D" : "3D";
			bool changed = MyGUI::StringCombo("次元", dimension, dimensions).valueChanged;
			if (changed) {
				request_.settings.is2D = dimension == "2D";
				if (request_.settings.is2D) {
					request_.parentTransform.localPos.z = 0;
					request_.parentTransform.localScale.z = 1;
					request_.parentTransform.localRotation = Quaternion::FromEulerDegrees({0, 0,
						Quaternion::ToEulerDegrees(request_.parentTransform.localRotation).z});
				}
				request_.forward = request_.settings.is2D ? LogicPlacementForwardAxis::PositiveX : LogicPlacementForwardAxis::PositiveZ;
				request_.rotationOffset = {};
			}
			MyGUI::InputText("親オブジェクトの名前", request_.parentName);
			EntityEditSetting reference;
			reference.acceptEntity = [&](ECSWorld& source, Entity object) {
				const auto* transform = source.TryGetComponent<TransformComponent>(object);
				return transform && ((transform->dimension == Dimension::Type2D) == request_.settings.is2D);
			};
			reference.rejectReason = "選択した次元に合うオブジェクトを設定してください";
			MyGUI::ObjectOrPrefabReferenceField("配置オブジェクト", request_.source.object, request_.source.prefab,
				world, context.toolContext.assetDatabase, reference);
			ImGui::SeparatorText("親オブジェクトの初期トランスフォーム");
			if (request_.settings.is2D) {
				Vector2 position(request_.parentTransform.localPos.x, request_.parentTransform.localPos.y);
				if (MyGUI::DragVector2("位置", position).valueChanged)
					request_.parentTransform.localPos = {position.x, position.y, 0};
				auto euler = Quaternion::ToEulerDegrees(request_.parentTransform.localRotation);
				if (MyGUI::DragFloat("回転", euler.z, {.dragSpeed = 1, .minValue = -360, .maxValue = 360}).valueChanged)
					request_.parentTransform.localRotation = Quaternion::FromEulerDegrees({0, 0, euler.z});
				Vector2 scale(request_.parentTransform.localScale.x, request_.parentTransform.localScale.y);
				if (MyGUI::DragVector2("スケール", scale).valueChanged)
					request_.parentTransform.localScale = {scale.x, scale.y, 1};
			} else {
				MyGUI::DragVector3("位置", request_.parentTransform.localPos);
				MyGUI::DragQuaternion("回転", request_.parentTransform.localRotation);
				MyGUI::DragVector3("スケール", request_.parentTransform.localScale);
			}
			ImGui::SeparatorText("配置設定");
			changed |= LogicPlacementFields::DrawShape(request_.settings);
			LogicPlacementFields::DrawOrientation(request_);
			if (changed || !preview_) preview_ = std::make_shared<LogicPlacementResult>(BuildLogicPlacement(request_.settings));
			TransformComponent sourceTransform;
			std::string diagnostic;
			const bool validSource = ResolveLogicPlacementSource(*world, context.toolContext.assetDatabase,
				request_.source, request_.settings.is2D, sourceTransform, diagnostic, &prefabCache_);
			if (!validSource) MyGUI::Text("配置元", diagnostic);
			if (!preview_->diagnostic.empty()) MyGUI::Text("配置", preview_->diagnostic);
			MyGUI::TextInt("配置数", static_cast<int32_t>(preview_->points.size()));
			ImGui::Separator();
			ImGui::BeginDisabled(!validSource || !preview_->IsValid());
			if (ImGui::Button("配置", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
				context.panelContext->host->ExecuteEditorCommand(std::make_unique<PlaceObjectsCommand>(request_));
			}
			ImGui::EndDisabled();
		}
	}
	ImGui::End();
	if (openWindow_ && context.panelContext && preview_ && preview_->IsValid()) {
		const auto& transform = request_.parentTransform;
		SubmitLogicPlacementPreview(*context.panelContext, preview_,
			Matrix4x4::MakeAffineMatrix(transform.localScale, transform.localRotation.Normalize(), transform.localPos), request_.settings.is2D);
	}
}

void LogicPlacementTool::LoadSettings() try {

	nlohmann::json data;
	if (!JsonAdapter::TryLoad(RuntimePaths::GetUserSettingsPath(ConfigPaths::kLogicPlacementTool), data) || !data.is_object()) return;
	request_.settings = data.value("settings", LogicPlacementSettings{});
	if (!BuildLogicPlacement(request_.settings).IsValid()) request_.settings = {};
	request_.parentName = data.value("parentName", std::string("Object"));
	request_.parentTransform = data.value("parentTransform", TransformComponent{});
	request_.orientation = data.value("orientation", LogicPlacementOrientation::Original);
	request_.forward = data.value("forward", request_.settings.is2D ? LogicPlacementForwardAxis::PositiveX : LogicPlacementForwardAxis::PositiveZ);
	request_.rotationOffset = Vector3::FromJson(data.value("rotationOffset", nlohmann::json{}));
	if (request_.orientation > LogicPlacementOrientation::Center) request_.orientation = LogicPlacementOrientation::Original;
	if (request_.forward > LogicPlacementForwardAxis::NegativeZ) request_.forward = LogicPlacementForwardAxis::PositiveX;
	if (request_.settings.is2D) {
		if (request_.forward == LogicPlacementForwardAxis::PositiveZ || request_.forward == LogicPlacementForwardAxis::NegativeZ)
			request_.forward = LogicPlacementForwardAxis::PositiveX;
		request_.rotationOffset.x = request_.rotationOffset.y = 0;
		request_.parentTransform.localPos.z = 0;
		request_.parentTransform.localScale.z = 1;
		request_.parentTransform.localRotation = Quaternion::FromEulerDegrees({0, 0,
			Quaternion::ToEulerDegrees(request_.parentTransform.localRotation).z});
	}
} catch (const std::exception&) {

	request_ = {};
}

void LogicPlacementTool::SaveSettings() const {

	const nlohmann::json data{{"settings", request_.settings}, {"parentName", request_.parentName},
		{"parentTransform", request_.parentTransform}, {"orientation", request_.orientation},
		{"forward", request_.forward}, {"rotationOffset", request_.rotationOffset.ToJson()}};
	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(ConfigPaths::kLogicPlacementTool), data);
}
