#include "DayNightCycleTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/DayNightCycleComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SkyAtmosphereComponent.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Systems/Lighting/DayNightCycleSystem.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Rendering/Renderer/Lighting/AtmosphereLighting.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>

// c++
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

	void Require(bool condition, const char* message) {

		if (!condition) throw std::runtime_error(message);
	}

	bool Near(float actual, float expected) {

		return std::abs(actual - expected) < 0.0001f;
	}
}

bool NEMTests::TestDayNightCycle() {

	using namespace Engine;
	try {
		DayNightCycleComponent clock;
		UpdateDayNightClock(clock, 60.0, true);
		Require(clock.runtimeCurrentHour == 12.0f, "開始時刻で初期化");
		UpdateDayNightClock(clock, 600.0, true);
		Require(Near(clock.runtimeCurrentHour, 0.0f), "一日の長さと日付の循環");
		clock.runtimeRequest = 2;
		UpdateDayNightClock(clock, 100.0, true);
		Require(clock.runtimeCurrentHour == 0.0f && clock.runtimePaused, "一時停止");
		clock.runtimeRequest = 3;
		UpdateDayNightClock(clock, 300.0, true);
		Require(clock.runtimeCurrentHour == 6.0f && !clock.runtimePaused, "再開");
		clock.runtimeSeekHour = -1.0f;
		clock.runtimeRequest = 6;
		UpdateDayNightClock(clock, 100.0, true);
		Require(clock.runtimeCurrentHour == 23.0f, "負の時刻を一日へ折り返す");
		clock.runtimeRequest = 4;
		UpdateDayNightClock(clock, 300.0, true);
		Require(clock.runtimeCurrentHour == 23.0f && !clock.runtimePlaying, "停止中は時刻を保持");
		clock.runtimeRequest = 5;
		UpdateDayNightClock(clock, 300.0, true);
		Require(clock.runtimeCurrentHour == 12.0f && !clock.runtimePlaying, "リセットは開始時刻へ戻して停止");
		clock.fixedTime = true;
		clock.startHour = 18.0f;
		UpdateDayNightClock(clock, 300.0, true);
		Require(clock.runtimeCurrentHour == 18.0f, "固定時刻");
		const auto sunrise = EvaluateDayNightDirection(6, 0, 60);
		const auto noon = EvaluateDayNightDirection(12, 0, 60);
		const auto sunset = EvaluateDayNightDirection(18, 0, 60);
		const auto midnight = EvaluateDayNightDirection(0, 0, 60);
		Require(Near(sunrise.y, 0) && Near(sunset.y, 0) && noon.y < -0.86f && midnight.y > 0.86f, "太陽の軌道");
		Require(Near(sunrise.x, -sunset.x), "日の出と日没の方位");
		const nlohmann::json savedClock = clock;
		const auto restoredClock = savedClock.get<DayNightCycleComponent>();
		Require(!restoredClock.runtimeInitialized && restoredClock.startHour == 18.0f && !savedClock.contains("runtimeHour"),
			"実行中の値はシーンへ保存しない");
		ECSWorld world(ECSWorldKind::Runtime);
		const auto owner = SceneAuthoring::CreateGameObject(world, "Cycle");
		const auto sun = SceneAuthoring::CreateGameObject(world, "Sun");
		const auto moon = SceneAuthoring::CreateGameObject(world, "Moon");
		const auto sky = SceneAuthoring::CreateGameObject(world, "Sky");
		world.AddComponent<DayNightCycleComponent>(owner);
		world.AddComponent<TimeScaleComponent>(owner);
		world.AddComponent<DirectionalLightComponent>(sun);
		world.AddComponent<DirectionalLightComponent>(moon);
		world.AddComponent<SkyAtmosphereComponent>(sky);
		auto& cycle = world.GetComponent<DayNightCycleComponent>(owner);
		cycle.sunEntity = SceneEntityReference::FromEntity(world, sun);
		cycle.moonEntity = SceneEntityReference::FromEntity(world, moon);
		cycle.atmosphereEntity = SceneEntityReference::FromEntity(world, sky);
		const nlohmann::json originalSun = world.GetComponent<DirectionalLightComponent>(sun);
		const nlohmann::json originalSky = world.GetComponent<SkyAtmosphereComponent>(sky);
		DayNightCycleSystem system;
		SystemContext context;
		context.mode = WorldMode::Edit;
		context.unscaledDeltaTime = 100.0f;
		system.Update(world, context);
		Require(cycle.runtimeCurrentHour == 12.0f, "編集時は既定で時刻を進めない");
		system.Update(world, context);
		Require(cycle.runtimeCurrentHour == 12.0f, "固定した編集プレビュー");
		Vector3 controlled;
		Require(ResolveDayNightLightDirection(world, sun, controlled) && controlled.y < -0.86f, "太陽の描画方向");
		Require(ResolveDayNightLightDirection(world, moon, controlled) && controlled.y > 0.86f, "月は太陽から12時間差");
		Require(AtmosphereLighting::ResolveSun(world, sky, world.GetComponent<SkyAtmosphereComponent>(sky)) == sun &&
			AtmosphereLighting::ResolveMoon(world, sky, world.GetComponent<SkyAtmosphereComponent>(sky)) == moon, "空へ両光源を反映");
		Require(nlohmann::json(world.GetComponent<DirectionalLightComponent>(sun)) == originalSun &&
			nlohmann::json(world.GetComponent<SkyAtmosphereComponent>(sky)) == originalSky, "ライトと空の保存値を変更しない");
		cycle.playInEditMode = true;
		system.Update(world, context);
		Require(Near(cycle.runtimeCurrentHour, 14.0f), "編集時の時刻進行");
		cycle.timeSource.mode = TimeSourceMode::Hierarchy;
		world.GetComponent<TimeScaleComponent>(owner).timeScale = 0.5f;
		system.Update(world, context);
		Require(Near(cycle.runtimeCurrentHour, 15.0f), "ローカル時間チャンネル");
		DayNightCycleSystem::RestoreEditPreviews(world);
		Require(!ResolveDayNightLightDirection(world, sun, controlled), "保存前に編集プレビューを解除");
		context.mode = WorldMode::Play;
		context.deltaTime = 0.0f;
		system.Update(world, context);
		const auto conflict = SceneAuthoring::CreateGameObject(world, "Conflict");
		world.AddComponent<DayNightCycleComponent>(conflict).sunEntity = SceneEntityReference::FromEntity(world, sun);
		system.Update(world, context);
		Require(!ResolveDayNightLightDirection(world, sun, controlled) &&
			!world.GetComponent<DayNightCycleComponent>(owner).runtimeDiagnostic.empty(), "制御先の競合を検出");
		world.RemoveComponent<DayNightCycleComponent>(conflict);
		system.Update(world, context);
		Require(ResolveDayNightLightDirection(world, sun, controlled), "競合解除後に再適用");
		world.RemoveComponent<DayNightCycleComponent>(owner);
		Require(!ResolveDayNightLightDirection(world, sun, controlled), "削除直後に古い結果を返さない");
		system.Update(world, context);
		Require(!ResolveDayNightLightDirection(world, sun, controlled), "削除後の抽出状態を解除");
		const auto nativeOwner = SceneAuthoring::CreateGameObject(world, "NativeCycle");
		world.AddComponent<DayNightCycleComponent>(nativeOwner);
		auto& worlds = ManagedWorldRegistry::GetInstance();
		const auto handle = worlds.Register(world);
		const auto native = MakeNativeEntity(world, nativeOwner);
		const auto light = MakeNativeEntity(world, moon);
		const auto invalid = MakeNativeEntity(world, sky);
		Require(GeneratedComponentBindings::SetComponentProperty(native, 75, 9, &invalid, sizeof(invalid)) ==
			ManagedStatus::InvalidArgument, "C#の月参照をDirectionalLightへ制限");
		Require(GeneratedComponentBindings::SetComponentProperty(native, 75, 9, &light, sizeof(light)) ==
			ManagedStatus::Ok, "C#の月参照");
		worlds.Unregister(handle);
		std::cout << "[PASS] DayNightCycle\n";
		return true;
	} catch (const std::exception& error) {
		std::cerr << "[FAIL] DayNightCycle: " << error.what() << '\n';
		return false;
	}
}
