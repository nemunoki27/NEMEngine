//============================================================================
//	include
//============================================================================
#include <Windows.h>

#include <NEMEngineRuntime.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	// エンジンソースには触れず、公開ABIだけでエディタを起動する
	return NEM_RunEditor();
}