#include "IParticleEmitterShape.h"

//============================================================================
//	IParticleEmitterShape classMethods
//============================================================================

namespace Engine {

	void IParticleEmitterShape::InitParticle(Vector3& position, Vector3& direction,
		const ParticleEmitterSettings& settings, bool is2D,
		[[maybe_unused]] const ParticleSpawnIndex& spawnIndex) const {

		InitParticle(position, direction, settings, is2D);
	}
}
