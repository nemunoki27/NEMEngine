//============================================================================
//	include
//============================================================================
#include <Engine/Core/Framework.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	
	std::unique_ptr<Engine::Framework> app = std::make_unique<Engine::Framework>();
	app->Run();

	return 0;
}