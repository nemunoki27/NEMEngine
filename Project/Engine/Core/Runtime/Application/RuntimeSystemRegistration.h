#pragma once

namespace Engine {

	// front
	class SystemScheduler;
	class UIInputSystem;

	// Editorと製品実行で共通のECSシステムを登録する
	UIInputSystem* RegisterRuntimeSystems(SystemScheduler& scheduler);
} // Engine
