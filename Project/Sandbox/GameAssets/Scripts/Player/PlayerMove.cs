using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	PlayerMove
//============================================================================
public sealed class PlayerMove : ScriptBehaviour {

	[Label("移動速度")]
	[SerializeField]
	private float moveSpeed = 4.0f;

	// アニメーションクリップ再生
	private AnimationPlayer? animClipPlayer;

	// アニメーションに必要な機能、ゲームで使う場面
	//- AnimationPlayerで、Clipを追加できてクリップの再生設定が少ない
	//  名前、クリップ、再生速度、ループ形式だけではなく、再生速度の下に
	//  向き相対チェックボックスの設定を入れる。ループ形式をループにした場合、下に
	//  ループの繋ぎ補間のチェックボックス、補間するならその時間、方法、そしてループ回数。
	//  往復も往復回数を設定できるように、これらのパラメータはC#スクリプトでも扱えるように
	//- animClipPlayer.Play("Jump");のようにしたとき、animClipPlayerではY座標のみアニメーション
	//  するようにクリップ作成を行っているので、transform.localPosition += transform.forward * moveSpeed * Time.deltaTime;
	//  のような処理はUpdateでXZのみ更新されるようにしたい。できているならそのままでいいです。要相談
	//- 複数クリップの同時再生ができるようにしたい。要相談
	// アニメーションクリップツールバグ、足りない機能
	//- キーの複数選択入力+削除ができない
	//- ツール内でアニメーション再生チェック中、プロパティの削除したらアニメーションしていない
	//  元のシーンパラメータに戻って欲しいのに戻らずアニメーション途中のパラメータのまま
	//- カーブ生成のベイク操作をするとき、SelectedChannelにしているとき選択していないと
	//  先頭の要素で作成されてしまうが、SelectedChannel、SelectedTrackという項目を消して、
	//  適用先選択で、対象のチャネルを選べるようにしてください。Vector3ならX,Y,Z,
	//  QuaternionならAngleのみ、ColorならAlpha,RGBみたいな感じでお願いします。複数選択は不要です。
	//- Enum表示のOverride,Add,Multiplyは日本語表記にしてください
	//- カーブエディター表示内のStep項目ですが、0.001fにして固定でいいのでエディターのDrag項目を削除

	//========================================================================
	//	開始時処理
	//========================================================================
	public override void Start() {

		animClipPlayer = GetComponent<AnimationPlayer>();
	}
	//========================================================================
	//	毎フレーム更新処理
	//========================================================================
	public override void Update() {

		// アニメーション入力
		if (Input.GetKeyDown(KeyCode.Space) && !animClipPlayer.IsPlaying) {

			animClipPlayer.Play("Jump");
		}

		// 前方移動
		transform.localPosition += transform.forward * moveSpeed * Time.deltaTime;
	}
}