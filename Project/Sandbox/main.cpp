//============================================================================
//	include
//============================================================================
#include <Windows.h>

#include <NEMEngineRuntime.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	// エンジンソースには触れず、公開ABIだけで起動する
#if defined(_RELEASE)
	return NEM_RunGame();
#else
	return NEM_RunEditor();
#endif
}
