using NEMEngine;

namespace SandboxScripts;

public sealed class Check : MonoBehaviour {

	private sealed class ParticleInstance {

		public ParticleInstance(GameObject _instance, float _stopTime) {

			instance = _instance;
			stopTime = _stopTime;
		}

		public GameObject instance;

		private float stopTimer = 0.0f;
		private float stopTime;

		// 寿命が終了したらtrueを返す
		public bool Update(float deltaTime) {

			stopTimer += deltaTime;

			return stopTimer >= stopTime;
		}
	}

	[SerializeField]
	private Prefab particle;
	[SerializeField]
	private GameObject newGameObject;
	[SerializeField]
	private MeshRenderer newGameObjectRenderer;
	[SerializeField]
	private bool emitParticle = false;
	[SerializeField]
	private float stopTime = 1.0f;

	private List<ParticleInstance> particleInstances = new List<ParticleInstance>();

	private void Update() {

		if (emitParticle) {

			// パーティクル生成
			particleInstances.Add(
				new ParticleInstance(Instantiate(particle), stopTime)
			);

			emitParticle = false;
		}

		// パーティクル更新（後ろからループ）
		for (int i = particleInstances.Count - 1; i >= 0; i--) {

			ParticleInstance instance = particleInstances[i];

			// 寿命が終了していなければスキップ
			if (!instance.Update(Time.deltaTime)) {
				continue;
			}

			// GameObjectを破棄
			Destroy(instance.instance);

			// Listから削除
			particleInstances.RemoveAt(i);
		}
	}
}