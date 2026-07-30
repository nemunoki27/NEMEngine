//============================================================================
//	include
//============================================================================
#include <Windows.h>

#include <NEMEngineRuntime.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	// エンジンソースには触れず、公開ABIだけでゲームを起動する
	return NEM_RunGame();
}
