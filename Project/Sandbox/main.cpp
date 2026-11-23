//============================================================================
//	include
//============================================================================
#include <Windows.h>

#include <NEMGameBootstrap.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR arguments, int) {

	// エンジンソースには触れず、公開ABIだけで起動する
	return NEM_LaunchGame(arguments);
}
