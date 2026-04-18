//============================================================================
//	include
//============================================================================
#include <Engine/Core/Framework.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	
	std::unique_ptr<Engine::Framework> game = std::make_unique<Engine::Framework>();
	game->Run();

	return 0;
}