// Inspectorのまとまりと、手書きのC#操作経路を対応付ける
const both = (label, api, description) => ({ label, api: api.split(', '), mode: 'Editor / C#', description });
const editor = (label, description) => ({ label, api: [], mode: 'Editorのみ', description });
const view = (label, description) => ({ label, api: [], mode: 'Editor表示のみ', description });
const reading = (label, api, description) => ({ label, api: api.split(', '), mode: 'Editor表示 / C#読取専用', description });
const code = (label, api, description) => ({ label, api: api.split(', '), mode: 'C#のみ', description });

const rendering = [
	both('表示レイヤーと描画順', 'RenderingLayerMask, Layer, Order', 'RenderingLayerMaskはCameraのCullingMaskと照合する表示先のbitです。LayerとOrderは描画の並び順で、衝突タイプとは別です。'),
	both('Material・個別パラメータ', 'Material, MaterialInstance', 'Materialは共有Asset、MaterialInstanceはこのRendererの上書きです。色・Texture・発光などの公開Parameterを個別に変え、ほかの物体のMaterial設定を変えずに点滅や色違いを作れます。'),
	both('ブレンドモード・キュー', 'BlendMode, Queue', '透明度をどう合成するかと、どの描画段階へ送るかを指定します。Alphaを下げるだけで透過描画になるとは限らないため、Materialの表面方式と合わせます。'),
];
const camera = [
	both('有効・メインカメラ・優先度', 'Enabled, IsMain, Priority', '描画に参加させるかとCamera選別時の条件です。無効なCameraは描画しません。複数Cameraで同じ領域へ出す場合は、選別条件とViewportを確認します。'),
	both('カリングマスク', 'CullingMask', 'Rendererの表示レイヤーと一致する対象を描画します。分割画面で左右に別の物体を映すときは、各CameraのマスクとRenderer側の割当を分けます。'),
	both('近クリップ・遠クリップ', 'NearClip, FarClip', 'Cameraから描画する奥行きの範囲です。近づいた物体が消える場合はNearClip、遠い背景が消える場合はFarClipを確認します。NearClipは0より大きく、FarClipより小さくします。'),
	both('出力テクスチャ・画面分割', 'TargetTexture, ViewportX, ViewportY, ViewportWidth, ViewportHeight', 'TargetTextureは別Rendererへ貼るRenderTexture出力です。Viewportは画面内の位置と大きさを0〜1で指定します。左半分はX=0・幅=0.5、右半分はX=0.5・幅=0.5です。'),
	both('ポストプロセス・Render Passes', 'PostProcessEnabled, RenderPasses, renderPasses', 'PostProcessEnabledでCameraの後処理を使用するかを切り替えます。RenderPassesは構成Asset、renderPassesは実行中のPass操作です。Bloomなどを変更するときはFindPassで対象を特定します。'),
	both('露出・Pre-Exposure', 'ExposureMode, ExposureEV100, ExposureCompensation, UsePreExposure', '手動EV100と自動露出を切り替え、明るさと補正を調整します。自動では最小・最大EV100、明暗への順応速度を使います。数値の変化と色補正による見た目の変化を区別します。'),
	both('Filmic・色補正', 'FilmicSlope, FilmicToe, FilmicShoulder, FilmicBlackClip, FilmicWhiteClip, ColorFilter, ColorTemperature, ColorTint, ColorSaturation, ColorContrast, ColorGamma, ColorGain, ColorOffset', 'ハイライトと暗部の圧縮、色温度、彩度、RGB補正をCameraごとに調整します。複数項目を一度に変えず、露出を決めてからフィルターやコントラストを調整すると比較しやすくなります。'),
	view('ビュープロジェクト行列', 'Cameraの位置・投影から計算した結果です。この行列表示へ数値を直接入力する編集項目ではありません。'),
];

export const componentDetails = {
	Transform: [
		editor('次元', '2D向けか3D向けかを指定します。Inspectorの表示軸や関連処理の基準になります。現在のTransformのC# APIに次元を直接変更するプロパティはありません。'),
		both('位置・回転・スケール', 'localPosition, localEulerAngles, localScale', 'Inspectorでは親を基準にした値を編集します。位置のWorld値はposition、回転のWorld値はrotationです。eulerAngles系は度数、rotation系はQuaternionを使います。'),
		both('親の回転・スケールを無視', 'ignoreParentRotation, ignoreParentScale', '親へ追従しながら、指定した成分の継承だけを外します。UIや頭上表示など、親の拡縮をそのまま受けたくない配置に使います。'),
		view('ワールド行列', '親子階層を含めた最終的な変換結果です。直接編集せず、Transformの値や親子付けを変更します。'),
		code('親子付けと座標変換', 'SetParent, TransformPoint, InverseTransformPoint', 'SetParentのworldPositionStaysでWorld位置を保つかを指定します。ローカルの発射位置をWorldへ変換する場合はTransformPointを使います。'),
	],
	Spline: [
		both('閉じた曲線', 'closed', '最後の制御点から先頭へ戻る経路にします。周回する巡回ルートやParticleの循環移動に使います。'),
		both('制御点・追加・削除', 'pointCount, GetPoint, SetPoint', '各点の位置、入る接線、出る接線で曲がり方を決めます。位置と接線はSplineのローカル値です。C#ではpointCountを確保してから、範囲内の点をSetPointで変更します。'),
		code('距離から位置を取得', 'EvaluateDistance, tolerance', '経路上を進んだ距離からWorld位置と接線を得ます。速度×経過秒を距離へ加えると、移動する足場や巡回する物体へ応用できます。toleranceは曲線の距離計算精度の設定です。'),
	],
	ForceField: [
		both('種類・軸方向・強さ', 'type, axis, strength', 'Attractionは吸引、Repulsionは反発、Vortexは渦、Windは風です。軸方向と強さを指定し、Particle側へForceField Moduleを追加します。Rigidbodyへ直接力を加えるComponentではありません。'),
		both('範囲・減衰', 'range, falloff', '力が届く範囲と端へ向かう弱まり方です。Transformで配置し、範囲外の粒子まで引き寄せる設定ではないことを確認します。'),
		both('有効・対象Layer', 'enabled, layerMask', '有効なFieldのうち、Particleの対象マスクと一致するものだけを使います。描画の可視性とは別の選別です。'),
	],
	VectorField: [
		both('種類・FGA', 'preset, field', 'Vortexは渦、Windは風、Assetは外部FGAの方向場です。Asset方式のときにFGA参照欄を使います。Particle側にはVectorField Moduleを追加します。'),
		both('範囲・強さ・追従', 'size, intensity, tightness', '場の大きさ、流れの強さ、粒子速度を流れへ近づける度合いです。追従を強くすると元の発生初速度より方向場の動きが支配的になります。'),
		both('有効・対象Layer', 'enabled, layerMask', '作用させるFieldをParticle側のマスクで選別します。CPUの剛体用の流体計算ではありません。'),
	],
	HeightmapCollider: [
		both('高さ画像・範囲と高さ', 'heightmap, size', '画像から高さを得て、指定した大きさの地形としてGPU Particleと衝突させます。Transformで地形へ位置を合わせます。'),
		both('有効・対象Layer', 'enabled, layerMask', 'ParticleのCollision設定でHeightmapを使い、対象マスクを一致させます。通常のMeshやRigidbodyのCollision形状を自動生成する機能ではありません。'),
	],
	AudioSource: [
		both('クリップ・有効・開始時再生', 'Clip, Enabled, PlayOnAwake', '音声Assetを指定します。PlayOnAwakeは自動再生するかのbool設定で、即座に音を鳴らす関数ではありません。今鳴らすときはPlayやPlayOneShotを使います。'),
		both('ループ・音量・ピッチ', 'Loop, Volume, Pitch', 'Loopは繰り返し、Volumeは音量倍率、Pitchは再生速度・高さの倍率です。同じ効果音を重ねたいときはPlayOneShotを使います。'),
		both('空間ブレンド・距離・減衰', 'SpatialBlend, MinDistance, MaxDistance, RolloffMode', '空間ブレンド0は2D、1は3D側の再生です。3DではAudioListenerとの距離による減衰を使います。最小・最大距離と減衰方式を組み合わせ、遠距離で聞こえる範囲を調整します。'),
		editor('Inspectorの試聴', '編集用の再生確認です。Play中のAudioSourceの再生所有とは区別します。ゲームコードからの再生はPlay・Pause・UnPause・Stopを使います。'),
	],
	AudioListener: [
		both('有効', 'Enabled', '3D音響を聞く位置と向きに、このGameObjectのTransformを使うかを指定します。通常は操作Cameraへ置き、有効なListenerを1つに絞ります。'),
	],
	PerspectiveCamera: [
		...camera,
		both('投影方式・視野角・平行投影サイズ', 'ProjectionMode, FovY, OrthographicSize', 'Perspectiveは遠い物体ほど小さく見える投影、Orthographicは距離で見かけの大きさが変わらない投影です。モードに応じて視野角または平行投影サイズの編集欄へ切り替わります。'),
		both('反射・AO', 'ReflectionEnabled, ReflectionIntensity, ReflectionMaxDistance, ReflectionQuality, AmbientOcclusionEnabled, AmbientOcclusionIntensity, AmbientOcclusionRadius, AmbientOcclusionQuality', '反射は強さ・最大距離・品質、AOは遮蔽の強さ・半径・品質を調整します。Project側のRay Tracing設定とGPUの対応状況も必要です。'),
		both('反射の追加パラメータ', 'GetRenderingSettings, SetRenderingSettings', 'GetRenderingSettingsで得た設定のSetReflectionParameterなどを変更し、SetRenderingSettingsで反映します。Snapshotだけを書き換えてもCameraへ戻すまでは適用しません。'),
		editor('錐台スケール', 'SceneViewに表示するCameraの錐台の見やすさを調整します。ゲームの視野角や製品解像度を変える値ではなく、対応するC#公開プロパティはありません。'),
	],
	OrthographicCamera: [
		...camera,
		code('反射・AOの共通設定値', 'ReflectionEnabled, AmbientOcclusionEnabled', '共通の描画設定APIとして値を持っていますが、現在のこのInspectorには3D Cameraと同じ反射・AOの専用編集欄はありません。3D平行投影で使う場合はPerspectiveCameraのProjectionModeを選びます。'),
	],
	CameraController: [
		both('有効・編集中プレビュー・モード', 'Enabled, EditorPreview, Mode', 'Followは位置追従、LookAtは注視、FollowLookAtは両方を使います。EditorPreviewはPlayしていない編集画面での確認用です。'),
		both('Follow：対象・オフセット・軸マスク', 'Follow, FollowLookAt', '追従対象とそこからのずらし量、追従する軸を指定します。位置補間を使ってCameraの移動を追い付かせます。Modeごとに使用するFollow設定を変更します。'),
		both('Follow：入力回転', 'Follow, FollowLookAt', '入力回転の有無、入力補間、パッド・マウス感度、有効デバイス、自動選択、縦回転反転を指定します。位置追従だけが必要な演出では入力回転を無効にします。'),
		both('LookAt：対象・オフセット・回転補間', 'LookAt, FollowLookAt', '位置追従先とは別の注視対象を指定できます。回転補間で向きの変化を緩め、ロール固定でCameraの傾きを抑えます。'),
	],
	CameraShake: [
		code('追加と揺れの設定', 'Enable, Duration, Strength, EasingType', '専用Inspectorはありません。CameraのGameObjectへAddComponent<CameraShake>()で追加し、継続時間・XYZの強さ・Easingを指定してEnableを切り替えます。通常の位置追従とは別の加算効果です。'),
	],
	JointAttachment: [
		code('スキン付きの対象・Joint名', 'SkinnedEntityLocalFileID, JointName', '専用Inspectorはありません。SkinnedEntityLocalFileIDには番号を直接作らず、追従するGameObject参照を指定します。モデルに存在するJoint名を使い、武器などの追従先として設定します。'),
	],
	MeshRenderer: [
		...rendering,
		both('Mesh・Material・SubMesh', 'Mesh, GetSubMeshes, SetSubMesh, GetMaterialInstance', 'モデルを指定し、SubMesh単位で表示・Material・表面方式・Alpha閾値を調整します。C#ではGetSubMeshesで設定を取得し、変更した要素をSetSubMesh(index, settings)で戻します。Materialの個別値はGetMaterialInstance(index)で変更します。'),
		both('SubMeshのローカルSRT・UV', 'GetSubMeshes, SetSubMesh', 'モデル内の部位だけを移動・回転・拡縮したり、その部位のUVをずらしたりします。GameObject全体のTransformとは別です。nameやstableIDは対象部位を確認する情報です。'),
		both('Zプリパス・デカール受け取り', 'EnableZPrepass, receiveDecals, RenderFlags', 'Zプリパスは深度の事前描画、receiveDecalsは投影デカールの受け取りです。描画フラグはMaterialの描画方式と合わせます。'),
		editor('SubMeshの同時編集', '複数部位のMaterialをまとめて編集する操作用のチェックです。実行データのboolプロパティではありません。C#では対象の各MaterialInstanceやSubMesh設定へ変更を適用します。'),
		editor('マテリアルパラメータの再読み込み', 'モデルから得たMaterial情報を編集内容へ取り込むための操作です。ゲーム中の色変更の関数ではありません。個別の上書きを戻したくない場合は、再読み込み前に設定を確認します。'),
		view('モデル判定・描画方式・行列', 'モデルから判定した表面方式、解決後の描画方式、World行列・UV行列を確認します。表示中の計算結果へ直接入力する操作ではありません。'),
	],
	SpriteRenderer: [
		...rendering,
		both('表示・サイズ・ピボット', 'Visible, Size, Pivot', 'Materialの画像を板として表示します。Sizeは板の大きさ、Pivotは配置と回転の基準です。Transformの位置・回転と、画像のUV設定を分けて扱います。'),
		editor('サイズをテクスチャに合わせる', '設定中のベースカラーTextureの実サイズをSizeへコピーする補助ボタンです。未設定・未ロードの画像では実行できない場合があります。C#ではSizeへ必要な大きさを指定します。'),
	],
	TextRenderer: [
		...rendering,
		both('Font・テキスト・文字間隔', 'Font, Text, FontSize, CharSpacing, Pivot', 'Font Assetの収録文字で文章を配置します。FontSizeは文字の基準サイズ、CharSpacingは追加の字間です。未収録文字は代替文字になる場合があるため、スコアやUIで使う文字をFontへ含めます。'),
		both('描画次元・ワールドスケール', 'Dimension, WorldScale', '2D画面向けか3D空間向けかを選びます。3DではWorldScaleでFontのサイズをWorldへ合わせます。'),
		both('文字ごとの変形・UV', 'CharacterTransforms, UvPerCharacter', '文字単位の位置・回転・スケールを編集できます。UvPerCharacterはUVを文章全体ではなく各文字へ適用する指定です。C#の文字変形はCharacterTransformsの要素を変更します。'),
	],
	SkyboxRenderer: [
		both('表示・Cubemap・色', 'Visible, CubemapTexture, Color, RenderingLayerMask', 'CubemapをCameraの背景へ表示します。通常の2D画像ではなくCubemapを指定し、表示レイヤーとCameraのマスクを合わせます。'),
		both('IBL強度・物理環境光', 'IblIntensity, PhysicalEnvironmentEnabled', '背景として見える画像だけでなく、環境反射や環境光への使用を調整します。IblIntensityは物体のMaterial表現にも影響するため、背景色の変更と区別します。'),
	],
	DecalProjector: [
		both('Material・個別Material', 'material, MaterialInstance', 'デカール用Materialを使います。個別設定のベースカラー・法線・質感・発光Texture、適用フラグはMaterialInstanceのParameter上書きです。共有Material Assetの変更と区別します。'),
		both('表示・受け取り先レイヤー', 'renderingLayerMask, receiverRenderingLayerMask', '表示レイヤーはCameraへの選別、受け取り先は投影するRendererの選別です。受け手のreceiveDecalsも有効にします。'),
		both('大きさと奥行き・中心オフセット', 'size, pivot', '投影箱が受け取り面へ交差するよう配置します。奥行きを大きくしすぎると、手前の壁以外にも投影されやすくなります。向きはTransformで決めます。'),
		both('乗算色・不透明度・UV・重なり順', 'color, fadeFactor, uvScale, uvBias, uvRotation, sortOrder', 'Materialへさらに色とAlphaを掛けます。fadeFactorを時間で下げると消滅フェードに使えます。UVは貼り方、sortOrderは重なるデカールの並びです。'),
		both('描画距離・距離フェード・角度フェード', 'drawDistance, fadeScale, startAngleFade, endAngleFade', '遠距離や投影面の傾きに応じて目立たなくします。drawDistanceは表示距離、fadeScaleはその距離に対するフェード開始の割合、角度は開始から終了の間で薄くする範囲です。'),
	],
	LineRenderer: [
		...rendering,
		both('2D・World座標・親', 'Is2D, UseWorldSpace, ParentLocalFileID, IgnoreParentRotation, IgnoreParentScale', '点列を画面・Worldのどちらで扱うかと、基準になる親を指定します。ParentLocalFileIDへはGameObject参照を渡します。座標の基準が異なる点を同じ線へ混ぜないでください。'),
		both('点の追加・削除・位置・色・太さ', 'Points, AddPoint, SetPoints, UpdatePoint, Clear', '点を並べた順番で線を接続します。C#でレーザーや照準線を動かす場合は、作成済みの点配列を更新してSetPointsへ渡せます。太さ0の点は見えなくなるため確認します。'),
		both('始点と終点を閉じる', 'Loop', '最後の点から最初の点へも線を引きます。輪郭や範囲表示には有効、レーザーや開いた経路には無効を使います。'),
	],
	PrimitiveRenderer: [
		...rendering,
		both('形状・描画空間', 'Type, RenderSpace, receiveDecals', 'Plane・CrossPlane・Ring・Cylinder・Sphere・Hemisphere・Cubeから選びます。World3DとScreen2Dで座標の意味が異なります。描画形状を選んでもPhysicsの当たり形状は追加されません。'),
		both('Plane・CrossPlane', 'Plane, CrossPlane', 'Planeは大きさ・基準点・軸・X/Y分割、CrossPlaneは大きさ・基準点・板の枚数を指定します。板の向きはCameraやLightに対して確認します。'),
		both('Ring', 'Ring', '外周と内周の半径、開始角・終了角、分割数で輪や扇形を作ります。角度は度で指定し、内周を0にすると穴のない形へ近づきます。'),
		both('Cylinder', 'Cylinder', '上面・中心・下面半径とWeight、3つの色、高さ、展開角、円周・高さ分割、フタ、UV方式を指定します。円錐台や部分的な円柱にも使えます。'),
		both('Sphere・Hemisphere・Cube', 'Sphere, Hemisphere, Cube', 'Sphere系は半径と経度・緯度分割、Hemisphereは底面のフタも指定します。Cubeは大きさと基準点です。分割数を増やすほど形状生成と描画の負荷が増えます。'),
	],
	ParticleSystem: [
		both('開始時に再生・有効・表示', 'PlayOnAwake, Enabled, Visible', 'PlayOnAwakeはboolプロパティで、自動再生の設定です。system.PlayOnAwake = true; と書き、PlayOnAwake()とは呼びません。現在その場で再生する操作はPlay()やPlayOneShot()です。表示とシミュレーションの有効状態は別です。'),
		both('CPU/GPU・速度・時間・空間・終了時処理', 'SimulationTarget, PlaybackSpeed, UseUnscaledTime, SimulationSpace, StopAction', 'CPUとGPUで使えるModuleが変わります。非スケール時間はTimeScaleに依存しない進行です。空間はLocalが生成元へ追従、Worldは独立、PhaseSettingsは各Phaseの設定を使います。終了後はNone=保持、Disable=GameObject無効、Destroy=破棄を選びます。'),
		both('Seed', 'useAutoRandomSeed, randomSeed', '固定した見た目を比較する場合は自動生成を無効にしてSeedを揃えます。自動生成を有効にすると再生ごとに乱数が変わります。'),
		both('再生・単発・一時停止・停止・消去', 'Play, PlayOneShot, Pause, Stop, Clear', 'Playは再生、PlayOneShotは単発再生、一時停止は続きを保持します。Stopは新規発生の停止、Clearは発生済み粒子の消去です。StopEmittingAndClearを指定したStopも使えます。withChildrenは子のParticleSystemへ操作を伝えるかです。'),
		both('発生設定・Emitter形状', 'GetSettings, SetSettings', 'emitterに発生間隔・発生数・最大数・初速・オフセットと発生形状を持ちます。Emitter形状は粒子の出る位置を決めるもので、描画する粒子のMesh形状とは異なります。'),
		both('描画・Material・Texture・表面', 'GetSettings, SetSettings', 'shape/model/material、描画空間、ブレンドとキュー、Billboard、TextureのUVを設定します。surfaceにはSoft Particle、照明・影・IBL、法線、MetallicとRoughnessがあります。'),
		both('Trail', 'GetSettings, SetSettings', 'trailに描画方式、元の粒子の表示、消滅後の残留、UV方式・繰り返し距離、幅と色、軌跡点数・間隔・寿命、Material、容量とメモリ上限を持ちます。粒子数とは別の負荷が生じます。'),
		both('Phase・ペアレント・Module', 'GetSettings, SetSettings', 'Phaseの名前・寿命・終了時処理・Materialと、親への追従を設定します。更新Moduleは個別に有効化でき、並び順も保持します。CPU/GPUの対応はModule一覧を参照してください。'),
		both('GPU発生元・GPUイベント', 'GetSettings, SetSettings', 'sourceでMeshの頂点・表面、Camera、画像などの発生元を選びます。マスク、表面固定、速度・色の継承も指定します。feedbackはイベント経路、塗装、衝突デカールと各容量上限です。GPU向け機能であり、通常のC#接触通知と同じ即時処理ではありません。'),
		both('表示レイヤー・描画順', 'RenderingLayerMask, Layer, Order', 'Cameraへの選別と描画の並びを指定します。発生元やFieldを選ぶマスクとは別です。'),
		editor('編集中も再生・エミッター形状を描画', '編集時のPreviewと補助線の表示です。現在のParticleSystemの公開C# APIには、この2つを直接切り替えるプロパティはありません。ゲーム中の再生には再生APIを使います。'),
		reading('粒子数・Trail数・容量超過', 'hasValidStatistics, particleCount, trailCount, droppedTrailCount', 'GPUの統計は読戻し待ちになる場合があります。hasValidStatisticsを確認してから件数を読みます。Trail容量超過は点や軌跡を保持できなかった数を調べる診断です。'),
		view('粒子属性・軌跡点容量の目安MB', 'Inspectorでメモリ消費の規模を確認するための表示です。実際の予算は最大粒子数やTrailの点数・容量・メモリ上限で設定します。'),
	],
	ParticlePaintReceiver: [
		both('投影方式・投影範囲', 'projection, planarSize', 'MeshのUVか平面投影で描き込み先を決めます。UVの重複箇所へは同じ塗装が現れる場合があるため、モデルのUVと合わせて方式を選びます。'),
		both('Texture解像度・塗装の不透明度', 'resolution, opacity, enabled', '描き込みTextureは32〜2048の編集範囲です。解像度を上げるほどメモリが増えます。Particle側のfeedback.paintも有効にします。'),
		code('塗装を消去', 'Clear', '実行中に描き込まれた内容を消去します。元のMaterialや元画像を削除する操作ではありません。'),
	],
	FlipbookAnimation: [
		both('再生時間・ループ・間隔・終了後非表示', 'Enabled, Duration, Loop, LoopInterval, EndAnimUnDisplay, EasingType', '1枚の画像内のコマを順番に表示します。Durationはアニメーション全体の時間です。LoopIntervalは次の周回までの間隔、終了後非表示は単発の演出を残さない指定です。'),
		both('分割Y・分割Xの列', 'TilesY, tileColumns', '縦の分割数と各列の分割情報を画像の配置へ合わせます。C#ではtileColumnsのBuffer要素を編集します。'),
		editor('編集中も再生', 'Play前にコマ送りの見た目を確認する設定です。現在の公開C# APIに対応する切り替えプロパティはありません。'),
	],
	UVTransform: [
		both('位置・回転・スケール', 'Position, Rotation, Scale', '物体のTransformではなくTextureのUVを動かします。位置はUV上のずらし、Scaleは貼り方の倍率、回転は度数です。TextureのSamplerがRepeatかClampかでも端の見え方が変わります。'),
	],
	Billboard: [
		both('全軸・X/Y/Z', 'AxisMask', 'Cameraへ向けるために回転させる軸を選びます。立ち木や看板では必要な軸だけを有効にし、粒子やSpriteを常に正面へ向けたい場合は全軸を使います。'),
	],
	InvertedHullOutline: [
		both('色・幅・幅モード', 'Enabled, Color, Width, WidthMode', 'モデルを膨張した背面で輪郭を描きます。ModelUnitsはモデルの大きさ、ScreenPixelsは画面上のpxを基準にするため、距離による太さの変化が異なります。'),
		both('膨張方式・Camera Z Offset', 'ExpansionMode, CameraZOffset', '法線方向へ押し出すか、位置の拡大を使うかを選びます。Z Offsetは深度の重なりを調整するための値で、幅の代用ではありません。'),
		both('Materialの透明度・Alpha閾値', 'RespectMaterialSurface, AlphaThreshold', '透明度を反映すると、MaterialのAlphaClipなどを基準にします。無効時は形状を基準にするため、葉の画像の透明部分まで輪郭が出る場合があります。'),
		both('Baked Normal・部位別幅・Stencil', 'UseBakedNormal, BakedNormalTexture, UseOutlineSampler, OutlineSamplerTexture, UseStencil', '専用Textureで膨張方向や部位別の幅を補助します。Stencil抑制は内側の重なりを抑える指定です。通常のBaseColor Textureをそのまま専用データの代用にはしません。'),
	],
	ScreenSpaceOutline: [
		both('色・幅・優先順位', 'Enabled, Color, WidthPixels, Priority', '画面上のMaskを使って輪郭を作ります。幅はpxです。複数のOutlineが重なったときはPriorityと描画条件を確認します。'),
		both('表示方式・領域方式', 'VisibilityMode, RegionMode', '現在の表示方式はVisibleOnlyです。領域は見えている輪郭全体か外周優先を選びます。壁の向こうへ透けるOutlineが自動で提供される設定ではありません。'),
		both('Alpha判定元・閾値・UI重なり', 'AlphaSource, AlphaThreshold, UiOcclusionMode', '透明部分をMaskへ含める基準と、UIに重なるときの合成を調整します。InvertedHullOutlineとはMaskの作り方が異なるため、同じ閾値でも結果を比較してください。'),
	],
	Collision: [
		both('Componentの有効・静的・押し戻し・衝突タイプ', 'Enabled, IsStatic, EnablePushback, TypeMask', 'Component全体を判定へ参加させるか、固定物として扱うか、接触時に押し戻すかを指定します。TypeMaskはプロジェクトの衝突設定で定義したbitで、描画レイヤーとは異なります。'),
		both('形状：タイプ', 'Shape', 'Shape.TypeでCircle2D・Quad2D・Sphere3D・AABB3D・OBB3D・Capsule2D・Capsule3Dを選びます。1つのCollisionにつき形状は1つです。描画Meshから自動生成するMesh Colliderではありません。'),
		both('形状：有効・コールバックのみ', 'Shape', 'Shape.Enabledは形状自身の有効状態です。Shape.IsTrigger=trueは押し戻さず接触通知だけを受ける指定で、拾い物・チェックポイント・侵入範囲に使います。'),
		both('形状：回転の使用・オフセット・回転', 'Shape', 'Shape.UseTransformRotationでTransform回転を含めるかを決め、OffsetとRotationDegreesで追加の位置・回転を指定します。RotationDegreesは度数です。Capsule2DではXY位置とZ回転を使います。'),
		both('Circle2D・Sphere3D：半径', 'Shape', 'Shape.Radiusで円・球の半径を指定します。サイズではなく中心から外周までの長さで、実際の判定にはTransformのScaleも影響します。'),
		both('Quad2D：半サイズ・回転四角', 'Shape', 'HalfSize2Dは全幅・全高の半分です。RotatedQuadを有効にすると回転した矩形として扱います。幅2・高さ1なら半サイズは(1, 0.5)です。'),
		both('AABB3D・OBB3D：半サイズ', 'Shape', 'HalfExtents3Dは全サイズの半分です。AABBはWorldの軸に沿った箱、OBBは回転する箱です。回転した壁や箱へ判定を合わせたい場合はOBBと回転設定を確認します。'),
		both('Capsule2D：サイズ・方向', 'Shape', 'CapsuleSize2Dはカプセル全体の幅・高さです。CapsuleAxisはX=横、Y=縦で、3D用の半径・高さとは別の値を使います。'),
		both('Capsule3D：半径・高さ・方向', 'Shape', 'Radiusは胴の半径、CapsuleHeightは両端の丸みを含む全高、CapsuleAxisはローカル長軸です。全高が直径より小さい場合、中央の線分がなくなって球に近い形になります。'),
		code('接触状態・接触コールバック', 'IsColliding', 'IsCollidingは現在frameの接触状態です。開始・継続・終了を使い分けたいときはMonoBehaviourのOnCollisionEnter・OnCollisionStay・OnCollisionExitを使い、引数Collisionから相手や接触点を取得します。'),
	],
	Rigidbody: [
		both('種別・衝突検出', 'BodyType, CollisionDetection', 'Dynamicは物理で動く物体、Kinematicはゲーム処理で動かす物体、Staticは固定物向けです。Discreteは通常の離散判定、Continuousは高速移動時のすり抜けを抑えるための方式です。Collisionの3D形状も必要です。'),
		both('質量・重力・速度減衰', 'Mass, UseGravity, GravityScale, LinearDamping, AngularDamping', '質量と重力倍率、移動・回転の減衰を指定します。減衰を大きくすると速度が落ちやすくなります。固定更新で力を加え、描画frameごとにTransformで結果を上書きしないようにします。'),
		both('摩擦・はね返り・初速度', 'Friction, Restitution, LinearVelocity', '接触時の滑りやすさ、跳ね方、初期の移動速度です。Frictionは描画MaterialのRoughnessとは別の物理値です。'),
		both('位置固定・倒れ方', 'FreezePositionX, FreezePositionY, FreezePositionZ, AllowTopple', '移動させたくない軸を固定します。AllowToppleは支点が外れた場合の倒れ方の指定です。全軸固定とStatic/Kinematicの目的を混同しないでください。'),
		code('力・トルク', 'AddForce, AddTorque, AngularVelocity', 'Forceは継続した力、Impulseは一度の衝撃などに使います。入力をUpdateで受け、FixedUpdateへ要求を渡すと、描画FPSに応じた入力の取りこぼしを避けられます。'),
	],
	Rigidbody2D: [
		both('種別・衝突検出・質量・重力', 'BodyType, CollisionDetection, Mass, UseGravity, GravityScale', '2D形状のCollisionと組み合わせ、XY方向で動かします。BodyTypeはDynamic/Kinematic/Staticです。3D用Rigidbodyを同じ物体の移動処理へ重ねないでください。'),
		both('摩擦・はね返り・減衰・初速度', 'Friction, Restitution, LinearDamping, AngularDamping, LinearVelocity', '2Dの移動速度はVector2です。接触時の滑りや跳ね方と、移動・回転の減衰を調整します。'),
		both('位置・回転固定・倒れ方', 'FreezePositionX, FreezePositionY, FreezeRotation, AllowTopple', '2Dキャラクターを立たせたまま動かす場合などに回転を固定します。回転を使いたい物体では固定を外します。'),
		code('力・トルク', 'AddForce, AddTorque, AngularVelocity', 'AddForceはVector2、トルクと角速度は2D用の数値です。3DのVector3用APIと引数を混ぜないでください。'),
	],
	FixedJoint: [
		both('接続先・有効・支点', 'ConnectedBody, Enabled, Anchor', '別の剛体へ固定してつなぎます。接続先はGameObject参照です。Anchorは自分側の支点で、物体の見た目だけでは接続先にRigidbodyがあることは保証されません。'),
		both('接続先支点の自動設定', 'AutoConfigureConnectedAnchor, ConnectedAnchor', '有効時は接続時の配置から接続先の支点を決めます。無効時はConnectedAnchorを自分で指定し、双方の基準位置を合わせます。'),
	],
	HingeJoint: [
		both('接続先・支点・接続先支点', 'ConnectedBody, Anchor, AutoConfigureConnectedAnchor, ConnectedAnchor, Enabled', '扉などの回転接続に使います。支点を扉の端へ合わせ、自動設定か手動指定で接続先の支点も合わせます。'),
		both('回転軸・角度制限', 'Axis, UseLimits, MinAngle, MaxAngle', 'Axisは回転を許す軸です。角度制限を有効にするとMinAngle〜MaxAngleに制限します。最小と最大の順番、軸の向き、支点の位置を一緒に確認します。'),
	],
	Canvas: [
		both('有効・ソートレイヤー・描画順', 'Enabled, SortingLayer, Order', '子のUIをまとめる基準です。別Canvasとの並びと、Canvas配下のボタン・文字の関係を設定します。'),
		code('基準解像度・拡縮', 'ReferenceResolution, ScaleMode, ScaleFactor, MatchWidthOrHeight', '現在のInspectorは基準解像度を表示するだけです。C# APIでは基準解像度・拡縮方式・倍率・幅高さの合わせ方も扱えます。表示情報と編集欄を区別してください。'),
		both('Player・ゲーム入力ブロック', 'PlayerIndex, InputBlockMode', 'Playerは0〜3です。入力ブロックは、なし・UI操作を消費したframe・Canvas有効中から選び、そのPlayerのゲーム入力を抑えます。UI自身の入力を止める指定ではありません。'),
		both('入力有効・決定後ロック・リピート', 'InputInEditMode, BlockInputAfterSubmit, RepeatDelay, RepeatInterval', '編集時にも操作するか、決定後の再操作を抑えるか、長押しの移動開始待ちと繰り返し間隔を指定します。C#ではInputLockedで状態を読み、BlockInputAfterSubmitをfalseへ戻すと入力ロックが解除されます。'),
		both('初期UI・ナビゲーション・ループ', 'FirstSelected, NavigationMode, WrapNavigation, TransitionTable', '最初に選択するUIと、自動の位置判定か遷移テーブルかを選びます。テーブルの縦横と各セルで選択先を定義し、端から反対側へ戻すかをループで指定します。'),
		both('キーボード・パッド・ボタン割当', 'KeyboardInputEnabled, GamepadInputEnabled, GamepadLeftStickEnabled, StickThreshold, SetKeyboardInputs, SetGamepadInputs', '選択移動・決定などへキーやパッドボタンを割り当てます。スティック使用時はしきい値で小さな入力を除きます。既存割当の取得・追加・削除APIも下表にあります。'),
	],
	UISelectable: [
		both('入力操作対象', 'Interactable', '無効にすると選択・決定の対象から外します。UI全体をGameObjectごと非アクティブにすることとは別です。'),
		both('通常・選択・決定・無効のスタイル', 'NormalColor, SelectedColor, SubmittedColor, DisabledColor', '各状態で色、スケール、変化時間、Easingを指定します。Normal/Selected/Submitted/Disabledの接頭辞が、C#プロパティの状態を表します。'),
		both('Texture・音・Animation Clip', 'NormalTexture, SelectedTexture, SubmittedTexture, DisabledTexture, SelectedSound, SelectedAnimationClip', 'Texture上書きの有無、状態移行時の音と音量、色・拡縮補間またはAnimation Clipを選びます。各状態のUseAnimationClipやAnimationEnabledで使用する経路を確認します。'),
		code('状態の取得', 'State, SelectedThisFrame, SubmittedThisFrame, DisabledThisFrame, NormalThisFrame', '現在の状態や、このframeに移行した状態を読み取れます。状態が続く間の判定と、そのframeだけの通知を混同しないでください。'),
	],
	UIImageButton: [
		both('有効・アクション名', 'Enabled, ActionName', 'Canvas配下のUISelectableとSpriteRendererを組み合わせます。アクション名はボタンの処理を区別する名前です。名前を設定しただけでC#の同名メソッドを自動実行する指定ではありません。'),
		code('決定を受け取る', 'WasClicked, ClickedThisFrame', 'Updateで決定したframeを確認して、メニューを閉じるなどの処理を行います。毎frameの長押し処理として扱わないようにします。'),
	],
	UITextButton: [
		both('有効・アクション名', 'Enabled, ActionName', 'Canvas配下のUISelectableとTextRendererを組み合わせる文字ボタンです。表示する文字列はTextRendererへ設定します。'),
		code('決定を受け取る', 'WasClicked, ClickedThisFrame', 'C#で決定したframeを確認し、画面遷移や設定変更へつなぎます。UIの選択・入力条件は親Canvasを確認します。'),
	],
	UIProgress: [
		both('最小・最大・現在値・方向', 'MinValue, MaxValue, Value, Direction, Enabled', 'HPなどの数値を最小〜最大の割合へ変換して、指定方向から満たす表示です。画像のMaterialや描画対象も用意します。'),
		both('補間・遅延表示', 'Smooth, SmoothDuration, SmoothEasing, Delayed, DelayedTexture, DelayedWait, DelayedDuration, DelayedEasing', '通常の表示を追い付かせる補間と、ダメージ後に遅れて減る表示を別々に調整します。遅延Textureはその見た目、Waitは動き始めるまでの秒数です。'),
		both('非スケール時間・編集時Preview', 'UseUnscaledTime, PreviewInEditMode', 'TimeScaleの影響を受けないUI進行と、Play前のPreviewを指定します。実行中の現在値と保存された初期値を区別します。'),
		code('表示中の割合・遅延先', 'DisplayedNormalizedValue, DelayedNormalizedValue, DelayedTargetLocalFileID', '補間後の表示割合は取得用です。遅延先はGameObject参照として扱い、保存用IDをゲームコードで直接組み立てません。'),
	],
	SkinnedAnimation: [
		both('クリップ・有効・ループ・速度・遷移時間', 'Clip, Enabled, Loop, PlaybackSpeed, TransitionDuration', 'スキン付きモデル内のAnimationを再生します。選択肢は読み込んだモデルのClipで、Animation Clip Assetを設定するAnimationPlayerとは別です。'),
		editor('編集中に再生・骨表示', 'Play前のAnimation確認と骨の可視化です。現在のSkinnedAnimationの公開C# APIに、この2つを直接変更するプロパティはありません。'),
		code('再生・停止・状態', 'Play, Stop, CurrentState, CurrentTime, IsPlaying, Finished, InTransition, RepeatCount, GetDuration', 'Clip名で再生し、遷移中か・再生時間・完了を確認できます。モデル内にないClip名を指定しても目的の動きは出ないため、Inspectorの候補を先に確認します。'),
	],
	AnimationPlayer: [
		both('有効・開始時再生・全体速度・開始グループ', 'enabled, PlayOnStart, speed, DefaultGroup', 'C#の操作型はAnimatorです。保存したグループを名前で再生し、全体速度を個々のStateの速度へ掛けます。'),
		both('グループ・State・Clip・weight・優先度', 'GetGroups, SetGroups', 'StateごとのClip、再生速度、weight、優先度、Additive、向き相対、開始遅延を設定します。設定を取得して編集した後はSetGroupsで反映します。'),
		both('ループ形式・回数・インターバル・繋ぎ補間', 'GetGroups, SetGroups', '一度だけ・ループ・往復などの再生形式をStateに設定します。ループの繋ぎ補間は終端から次の開始までの補間で、回数や待機時間とは別です。'),
		editor('編集中に再生', 'Play前にClipの見た目を確認する設定です。現在のAnimatorの公開C# APIに対応するPreview用プロパティはありません。'),
		code('Play・PlayClip・CrossFade・Seek・Stop', 'Play, PlayClip, CrossFade, CrossFadeInFixedTime, Seek, Stop', 'State名での再生、Clipの直接再生、遷移、再生位置指定、停止に使います。CrossFadeの遷移量は正規化値、CrossFadeInFixedTimeは秒指定なので引数を取り違えないでください。'),
	],
	DirectionalLight: [
		both('色・方向・強度・有効', 'Color, Direction, Intensity, Enabled', '太陽のように一定方向から照らす光です。Directionは光の進む向きで、Cameraの注視方向や点光源の位置とは別です。'),
		both('影の強さ・光源角度', 'ShadowStrength, ShadowAngularRadius', '影の濃さと、光源の広がりによる半影を調整します。影の計算方式はProjectのRay Tracing設定にも依存します。'),
	],
	PointLight: [
		both('色・強度・有効・位置', 'Color, Intensity, Enabled', 'Transformの位置から周囲へ広がる光です。壁灯・爆発・アイテムの点滅などに使います。位置を動かすときはTransformを変更します。'),
		both('半径・減衰・光源半径・影', 'Radius, Decay, ShadowRadius, ShadowStrength', 'Radiusは照らす範囲、Decayは弱まり方、ShadowRadiusは影計算での光源の大きさです。光源半径を変えることと照射範囲を広げることは異なります。'),
	],
	RectLight: [
		both('色・総出力・有効・向き', 'Color, Intensity, Enabled', '矩形の面光源です。Transformで位置と向きを決めます。同じ総出力でも光源面の幅・高さを変えると見え方が変わります。'),
		both('減衰半径・光源幅・光源高さ', 'AttenuationRadius, SourceWidth, SourceHeight', '照射の届く範囲と、発光面の寸法を別々に指定します。窓や照明パネルのような広がった光に使います。'),
		both('バーンドア角度・長さ・影', 'BarnDoorAngle, BarnDoorLength, ShadowStrength', '光源の縁で照射を制限するための設定です。光源面の寸法と合わせ、単に強度を下げる調整とは区別します。'),
	],
	SpotLight: [
		both('色・方向・強度・有効', 'Color, Direction, Intensity, Enabled', 'Transformの位置から指定方向の円錐へ照らします。懐中電灯や舞台照明に向いています。'),
		both('距離・減衰・角度余弦・減衰開始余弦', 'Distance, Decay, CosAngle, CosFalloffStart', '照射距離と円錐の端の弱まり方を指定します。角度欄の値は角度そのものではなくcos値です。度数をそのまま代入しないでください。'),
		both('光源半径・影の強さ', 'ShadowRadius, ShadowStrength', '光源の大きさによる影の柔らかさと、影の濃さを調整します。照射距離とは別の値です。'),
	],
	Script: [
		both('有効', 'enabled', 'Scriptごとにコールバックの有効状態を切り替えます。GameObject全体のactiveSelfとは別です。'),
		editor('Scriptを開く・削除', 'ヘッダーの右クリックからソースを開くかScriptを外します。保存Fieldや属性はゲームのC#コードで定義し、Inspectorはその定義に従って編集欄を表示します。'),
		code('保存Field・属性・処理', 'gameObject, transform', 'SerializeFieldなどで公開するField、Updateや接触コールバック、Timer・Coroutineをゲーム側で実装します。属性一覧とScriptの書き方を参照してください。'),
	],
};

export const componentExamples = {
	Transform: ['FirstMover.cs', '入力で物体を移動する', 'このGameObjectへ追加し、Play中に左右キーで移動します。物理のDynamic Bodyを直接動かす例ではありません。'],
	Spline: ['SplinePatrol.cs', '経路に沿って物体を動かす', 'Splineの制御点をEditorで用意し、移動する物体のpathObjectへSplineのGameObjectを設定します。'],
	AudioSource: ['TriggerSound.cs', 'プレイヤーが範囲へ入ったときに音を鳴らす', 'AudioSourceへ音声を設定し、CollisionのShape.IsTriggerを有効にします。Playerタグを持つ相手との接触で一度再生します。'],
	PerspectiveCamera: ['CameraEffects.cs', 'CameraのBloomを実行中に変更する', 'CameraへBloomを含むRenderPassesを設定し、このScriptを同じGameObjectへ追加します。'],
	CameraController: ['FollowCamera.cs', 'プレイヤーの後方へCameraを追従させる', 'CameraとCameraControllerのあるGameObjectへ追加し、targetへプレイヤーを設定します。'],
	MeshRenderer: ['DamageFlash.cs', 'ダメージ時に一時的に赤くする', 'MeshRendererのある物体へ追加します。ゲームの被弾処理からFlash()を呼びます。MaterialがBaseColorを使うことを確認します。'],
	TextRenderer: ['ScoreText.cs', 'スコア表示を更新する', 'TextRendererへFontを設定し、このScriptへ得点を渡します。Fontには表示するSCOREと数字・記号を収録します。'],
	DecalProjector: ['DecalLifetime.cs', '傷を一定時間残してAlphaで消す', '傷のDecalProjectorと同じGameObjectへ追加します。Materialと投影範囲はEditorで設定します。'],
	LineRenderer: ['LaserLine.cs', '2点をつないでレーザーを追従させる', 'LineRendererのあるGameObjectへ追加し、endPointへ照射先を設定します。World座標とRendererのMaterialを使います。'],
	PrimitiveRenderer: ['MaterialTint.cs', 'このRendererだけ色を変える', 'PrimitiveRendererの個別Material設定へ色を適用します。ほかの物体が使う共有Material Assetの変更ではありません。'],
	ParticleSystem: ['ParticleBurst.cs', 'Eキーで単発の演出を再生する', '作成済みのParticleSystemと同じGameObjectへ追加します。自動再生設定と、その場での再生関数を分けています。'],
	UVTransform: ['UVScroll.cs', '川やベルトのTextureを流す', 'Textureを持つRendererとUVTransformのあるGameObjectへ追加します。画像を繰り返すにはSamplerをRepeatへ設定します。'],
	Collision: ['TriggerPickup.cs', 'Playerだけに反応する拾い物を作る', 'このScriptがSphere3DのTriggerを設定します。Playerタグのある相手にも3DのCollisionを用意し、衝突設定で接触する組み合わせにします。'],
	Rigidbody: ['PhysicsJump.cs', 'キー入力を固定更新へ渡して衝撃を加える', 'RigidbodyをDynamicにし、3DのCollisionを用意します。この例は衝撃の加え方です。連続ジャンプを制限する接地判定はゲーム側へ追加します。'],
	UIImageButton: ['SubmitMenu.cs', 'ボタンを決定するとメニューを閉じる', 'Canvas配下にUISelectableと画像ボタンを用意し、menuへ閉じたいメニューのルートを指定します。'],
	UITextButton: ['SubmitMenu.cs', '文字ボタンを決定するとメニューを閉じる', 'Canvas配下にUISelectableと文字ボタンを用意します。例はUIImageButtonとUITextButtonの両方を扱います。'],
	UIProgress: ['HealthBar.cs', 'ダメージでHPバーを減らす', 'UIProgressのあるGameObjectへ追加し、ApplyDamage(amount)をゲーム側の被弾処理から呼びます。補間と遅延表示も設定します。'],
	AnimationPlayer: ['ActionAnimation.cs', '操作に応じて作成済みのStateを再生する', 'AnimationPlayerへグループとStateを用意し、stateNameを存在するState名へ合わせます。C#ではAnimatorを取得します。'],
	PointLight: ['PulsingLight.cs', 'ランプの明るさを周期的に変える', 'PointLightと同じGameObjectへ追加します。照射範囲はEditorで設定し、ここでは強度だけを時間で変えます。'],
};

export const componentReferenceTypes = {
	Transform: ['GameObject', 'Space'],
	Spline: ['SplineControlPoint', 'SplineSample'],
	PerspectiveCamera: ['CameraRenderingSettings', 'CameraRenderPasses'],
	OrthographicCamera: ['CameraRenderingSettings', 'CameraRenderPasses'],
	MeshRenderer: ['SubMeshSettings', 'MaterialInstance', 'MaterialSurfaceMode'],
	SpriteRenderer: ['MaterialInstance'],
	TextRenderer: ['TextCharacterTransform', 'MaterialInstance'],
	DecalProjector: ['MaterialInstance'],
	LineRenderer: ['LinePoint', 'LineRendererPoint'],
	ParticleSystem: ['ParticleSystemSettings', 'ParticleEmitterSettings', 'ParticlePhaseSettings', 'ParticleModuleSettings', 'ParticleTrailSettings', 'ParticleSourceSettings', 'ParticleSurfaceSettings', 'ParticleFeedbackSettings', 'ParticleEventRouteSettings'],
	Collision: ['ColliderShapeType', 'CapsuleAxis', 'Collision'],
	Canvas: ['CanvasTransitionTable', 'CanvasInputAction'],
	AnimationPlayer: ['AnimationGroupSettings', 'AnimationStateSettings', 'AnimationClip', 'AnimatorStateInfo'],
	Script: ['SerializeFieldAttribute', 'FormerlySerializedAsAttribute'],
};
