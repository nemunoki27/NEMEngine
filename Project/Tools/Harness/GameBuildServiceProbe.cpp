//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Build/GameBuildService.h>

// c++
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {

	if (argc != 4) {
		std::cerr << "使い方: GameBuildServiceProbe <検証用ゲーム> <開始シーンGUID> <出力先>\n";
		return 1;
	}
	std::filesystem::current_path(Engine::Algorithm::PathFromUTF8(argv[1]));
	Engine::RuntimePaths::Refresh();
	Engine::AssetDatabase database{};
	if (!database.Init() || !database.RebuildMeta()) {
		std::cerr << "アセットデータベースの初期化に失敗しました\n";
		return 2;
	}
	Engine::GameBuildSettings settings{};
	settings.startupScene = Engine::FromString32Hex(argv[2]);
	settings.executableName = "ProductProbe";
	settings.outputRoot = Engine::Algorithm::PathFromUTF8(argv[3]);
	Engine::GameBuildService service{};
	std::string error;
	if (!service.Start(settings, database, error)) {
		std::cerr << error << '\n';
		return 3;
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
	while (service.IsBuilding()) {

		service.Update();
		if (std::chrono::steady_clock::now() >= deadline) {
			std::cerr << "製品ビルドの検証が時間切れになりました\n";
			return 4;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	if (service.GetState() != Engine::GameBuildState::Completed) {
		std::cerr << service.GetFailureDetail() << '\n';
		return 5;
	}
	std::cout << "Menubarと共通の製品ビルドが完了しました: " <<
		Engine::Algorithm::PathToUTF8(service.GetOutputDirectory()) << '\n';
	return 0;
}
