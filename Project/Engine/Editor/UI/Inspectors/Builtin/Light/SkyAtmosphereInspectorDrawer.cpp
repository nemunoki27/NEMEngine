#include "SkyAtmosphereInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Lighting/AtmosphereLighting.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>

//============================================================================
//	SkyAtmosphereInspectorDrawer classMethods
//============================================================================
void Engine::SkyAtmosphereInspectorDrawer::DrawFields([[maybe_unused]] const EditorPanelContext& context,
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity, bool& anyItemActive) {

	auto& draft = GetDraft();
	EntityEditSetting sunSetting;
	sunSetting.acceptEntity = [](ECSWorld& source, Entity target) {
		return source.TryGetComponentForBinding<DirectionalLightComponent>(target) != nullptr;
	};
	sunSetting.rejectReason = "DirectionalLightコンポーネントが必要です";
	DrawField(anyItemActive, [&]() {
		return SceneEntityReferenceFields::Draw("太陽", world, entity, draft.sunEntity, sunSetting);
	});
	if (!world.IsAlive(AtmosphereLighting::ResolveSun(world, entity, draft))) {
		MyGUI::Text("太陽", "有効なDirectionalLightがありません");
	}
	DrawField(anyItemActive, [&] { return InspectorDrawerCommon::DrawCheckboxField("月を表示", draft.moonDiskEnabled); });
	DrawField(anyItemActive, [&] {
		return SceneEntityReferenceFields::Draw("月", world, entity, draft.moonEntity, sunSetting);
	});
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("月の角度半径（度）", draft.moonAngularRadius,
		{.dragSpeed = 0.001f, .minValue = 0.01f, .maxValue = 45.0f}); });
	DrawField(anyItemActive, [&] { return MyGUI::ColorEdit("月の表示色", draft.moonDiskColor); });
	DrawField(anyItemActive, [&] { return MyGUI::DragFloat("月の表示明るさ", draft.moonDiskBrightness,
		{.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 10000.0f}); });
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("地表半径 km", draft.planetRadiusKm, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("大気高度 km", draft.atmosphereHeightKm, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("Rayleigh散乱係数 /km", draft.rayleighScattering);
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Rayleigh密度高度 km", draft.rayleighScaleHeightKm, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Mie散乱係数 /km", draft.mieScattering, { .dragSpeed = 0.0001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Mie吸収係数 /km", draft.mieAbsorption, { .dragSpeed = 0.00001f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Mie密度高度 km", draft.mieScaleHeightKm, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("Mie異方性", draft.mieAnisotropy, { .dragSpeed = 0.01f, .minValue = -0.95f, .maxValue = 0.95f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("オゾン吸収係数 /km", draft.ozoneAbsorption);
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("オゾン中心高度 km", draft.ozoneCenterHeightKm, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("オゾン層の厚さ km", draft.ozoneWidthKm, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::ColorEdit("地表色", draft.groundAlbedo);
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("IBL強度", draft.iblIntensity, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("遠景の霞の距離 m", draft.aerialPerspectiveDistance, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
	DrawField(anyItemActive, [&]() {
		return MyGUI::DragFloat("遠景の霞の強さ", draft.aerialPerspectiveStrength, { .dragSpeed = 0.01f, .minValue = 0.0f });
	});
}