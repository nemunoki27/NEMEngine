#include "ClothManagedTests.h"

//============================================================================
//	include
//============================================================================
#include "TestFixtures.h"

bool NEMTests::TestClothManaged() {

	return RunManagedComponentProbe("Cloth", "NEMTests.Scripts.ClothAPIProbe",
		"13663eaa-f905-4328-9315-cfcc22b5c1c3", "2ca2b4aa-6e68-46b6-88d8-203fbffdda05",
		[](Engine::ECSWorld&) {});
}
