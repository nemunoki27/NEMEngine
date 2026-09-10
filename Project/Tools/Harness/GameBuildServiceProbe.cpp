//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Editor/Build/GameBuildService.h>

// c++
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {

	if (argc != 4 && argc != 5) {
		std::cerr << "使い方: GameBuildServiceProbe <検証用ゲーム> <開始シーンGUID> <出力先> [--collect]\n";
		return 1;
	}
	// 配置前後で実際の衝突フィルターが一致するかを調べる
	if (std::string_view(argv[1]) == "--collision") {
		Engine::CollisionSettings source, product;
		source.SetActiveSettingsPath(Engine::Algorithm::PathFromUTF8(argv[2]));
		product.SetActiveSettingsPath(Engine::Algorithm::PathFromUTF8(argv[3]));
		if (source.GetTypeCount() != product.GetTypeCount()) return 6;
		for (uint32_t i = 0; i < source.GetTypeCount(); ++i) {
			if (source.GetTypes()[i].name != product.GetTypes()[i].name ||
				source.GetTypes()[i].enabled != product.GetTypes()[i].enabled) return 6;
			for (uint32_t j = 0; j < 32; ++j) {
				if (source.CanCollide(1u << i, 1u << j) != product.CanCollide(1u << i, 1u << j)) return 6;
			}
		}
		std::cout << "配置前後の衝突設定が一致しました: " << source.GetTypeCount() << '\n';
		return 0;
	}
	std::filesystem::current_path(Engine::Algorithm::PathFromUTF8(argv[1]));
	Engine::RuntimePaths::Refresh();
	Engine::DefaultMaterialSettings::GetInstance().Load("GameAssets/Materials/Config/defaultMaterials.materialSettings.json");
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
	if (argc == 5) {
		if (std::string_view(argv[4]) != "--collect") return 1;
		// 日本語パスも登録時と検索時で同じGUIDへ正規化されることを確認する
		for (const auto& [assetID, meta] : database.GetAssets()) {
			const std::string lookupPath = "./" + meta.assetPath;
			const auto* found = database.FindByPath(lookupPath);
			if (!found || found->guid != assetID ||
				database.ImportOrGet(meta.assetPath, meta.type) != assetID) return 8;
		}
		std::vector<Engine::GameBuildFileEntry> files;
		if (!Engine::GameBuildService::CollectFiles(settings.startupScene, database, files, error)) {
			std::cerr << error << '\n';
			return 3;
		}
		for (const auto assetID : Engine::BuiltinAssets::Runtime::Assets) {
			const auto source = database.ResolveFullPath(assetID);
			if (std::none_of(files.begin(), files.end(), [&source](const auto& file) { return file.source == source; })) return 7;
		}
		nlohmann::json result = nlohmann::json::array();
		for (const auto& file : files) {
			result.push_back({ { "source", Engine::Algorithm::PathToUTF8(file.source) },
				{ "destination", file.destination }, { "size", file.size }, { "sha256", file.sha256 } });
		}
		if (!Engine::JsonAdapter::SaveCanonical(settings.outputRoot, result)) return 4;
		std::cout << "製品ファイル収集の検証が完了しました: " << files.size() << '\n';
		return 0;
	}
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
