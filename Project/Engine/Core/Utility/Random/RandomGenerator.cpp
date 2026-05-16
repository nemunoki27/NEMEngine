#include "RandomGenerator.h"

using namespace Engine;

//============================================================================
//	RandomGenerator classMethods
//============================================================================

Vector3 RandomGenerator::Generate(const Engine::Vector3& min, const Engine::Vector3& max) {

	return Vector3{
	Generate(min.x, max.x),
	Generate(min.y, max.y),
	Generate(min.z, max.z) };
}

Color4 RandomGenerator::Generate(const Engine::Color4& min, const Engine::Color4& max) {

	return Color4{
		Generate(min.r, max.r),
		Generate(min.g, max.g),
		Generate(min.b, max.b),
		Generate(min.a, max.a) };
}
