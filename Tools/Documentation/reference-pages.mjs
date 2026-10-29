import fs from 'node:fs';
import path from 'node:path';
import { root, output, website, read, write, walk, formatDocumentation } from './paths.mjs';
import { describeProperty } from './component-notes.mjs';
import { names, modules } from './api-notes.mjs';

const slug = value => value.replace(/([A-Z]+)([A-Z][a-z])/g, '$1-$2').replace(/([a-z\d])([A-Z])/g, '$1-$2').replace(/[^\w-]+/g, '-').toLowerCase();
const escape = value => String(value ?? '').replaceAll('|', '&#124;').replaceAll('<', '&lt;').replaceAll('>', '&gt;').replaceAll('\n', ' ');
const table = (headers, rows) => `| ${headers.join(' | ')} |\n| ${headers.map(() => '---').join(' | ')} |\n${rows.map(row => `| ${row.map(escape).join(' | ')} |`).join('\n')}\n`;
const save = (file, title, text) => write(path.join(website, file), formatDocumentation(`# ${title}\n\n${text}\n`));

const toolGuides = {

	SceneViewCameraController: ['project-settings', 'プロジェクト設定 → シーンカメラ設定', '制作用Cameraの移動・回転・感度と保存状態を調整します。ゲームCameraの設定とは別です。'],
	CollisionManagerTool: ['physics', 'プロジェクト設定 → 衝突設定', '衝突タイプを定義し、接触を許可する組み合わせを表で設定します。表示レイヤーとは別です。'],
	AnimationClipTool: ['animation', 'ツール → アニメーション → アニメーションクリップ作成', '作成または参照したClipへTrackとKeyを追加し、Previewして保存します。'],
	RenderFeatureProfileTool: ['post-process', 'ツール → レンダリング → レンダーパス', 'CameraのRenderPassesと同じプロファイルを開き、Passと入力・出力・Parameterを編集します。'],
	MaterialEditorTool: ['materials', 'ツール → レンダリング → マテリアル作成', '用途に合う既定を選び、保存先と名前を指定してMaterialを作成します。'],
	ShaderGraphEditorTool: ['shader-graph', 'ツール → レンダリング → シェーダーグラフ', 'GraphのNodeを接続し、型と出力を確認してコンパイル・保存します。'],
	SceneCompositionTool: ['scene', 'ツール → シーン → シーン構成', 'Sceneの追加・解放・Active指定を行います。Scene AssetとInstanceを区別します。'],
	TagManagerTool: ['project-settings', 'プロジェクト設定 → タグ・描画レイヤー', 'タグと描画Layerを定義します。削除や名前変更は参照先へ影響するため内容を確認して保存します。'],
	PerformanceCheckTool: ['performance', 'ツール → デバッグ → パフォーマンスチェック', '描画条件を揃えて負荷を比較します。読込中と安定状態、表示Viewの数を区別します。'],
	ScriptExecutionOrderTool: ['scripting', 'プロジェクト設定 → スクリプト実行順', 'Script型ごとの実行順を指定し、保存します。値の小さい側と同値の扱いはゲームの依存を作りすぎないようにします。'],
	ScriptProfilerTool: ['performance', 'ツール → スクリプト → スクリプトプロファイラー', 'Scriptコールバックと計測Scopeの時間を比較します。GPU Passの時間とは別です。'],
	InputDeviceTool: ['input', 'プロジェクト設定 → 入力デバイス', 'Playerの操作機器と機器側の入力条件を設定します。Action名の定義と現在の入力値も合わせて確認します。'],
};

const moduleDetails = {

	AlphaReference: ['Alphaの切り抜き閾値を寿命で変え、画像が現れる・消える演出を作ります。', '開始・終了の閾値とEasingを設定します。MaskedのMaterialで、Alphaのある画像を使います。', 'ParticleAlphaReferenceSettings'],
	ColorOverLifetime: ['寿命に応じてRGBA色を変えます。', '開始色と終了色、またはCurveを設定します。消したい場合は終了Alphaを0へ近づけます。', 'ParticleColorSettings'],
	ColorUV: ['UVの移動・回転・拡縮でTextureの見え方を変えます。', 'UVトランスフォーム内で位置・回転・拡縮を調整し、回転中心やスクロール方式を設定します。', 'ParticleUVTransformSettings'],
	CustomData: ['Shaderへ追加の2組の4成分データを渡します。', 'Shader側で対応入力を使用し、成分と寿命変化を設定します。値を渡すだけではShaderの見た目は変わりません。', 'ParticleCustomDataSettings'],
	CustomShaderParameter: ['指定したShader Parameterを粒子の設定から制御します。', 'Shaderで公開している正確なParameter名と型を合わせます。', 'ParticleShaderParametersSettings'],
	Emissive: ['色と発光強度を寿命で変えます。', '「現在の色で発光」は現在の粒子RGBを参照します。開始・終了の強度は別に指定し、Bloomでにじみを確認します。', 'ParticleEmissiveSettings'],
	Flipbook: ['画像をタイル分割してコマAnimationにします。', '横・縦タイル数と寿命内の周回数を指定します。画像の並びと分割数を合わせます。', 'ParticleFlipbookSettings'],
	GravityForce: ['速度へ重力加速度を加えます。', '重力Vectorを設定します。水平面反射を使う場合は地面の高さと反発係数も設定します。', 'ParticleGravitySettings'],
	LookToVelocity: ['粒子を移動方向へ向けます。', '初速や重力などで移動する粒子へ追加します。速度がほぼ0のときは方向を決められません。', 'ParticleLookToVelocitySettings'],
	CurlNoise: ['渦状のノイズを速度へ加えます。', '周波数で渦の細かさ、強さで変位、スクロール速度で変化を調整します。', 'ParticleCurlNoiseSettings'],
	NoiseForce: ['ノイズの力で粒子を揺らします。', '周波数と強さを小さい値から上げ、意図した軌道を崩しすぎないようにします。', 'ParticleNoiseForceSettings'],
	NoiseUV: ['TextureのUVへノイズの変位を加えます。', '周波数と強さを設定します。粒子のWorld位置を揺らすNoiseForceとは別です。', 'ParticleNoiseUVSettings'],
	PendulumMovement: ['振り子のような周期運動を作ります。', '長さ、最大角、周期、開始位相、平面の角度と粒子ごとの位相差を設定します。', 'ParticlePendulumSettings'],
	Rotation: ['固定回転や寿命に沿う回転、回転速度を設定します。', 'Eulerの軸・角度とQuaternionを選び、角度Curveと速度Curveを区別します。', 'ParticleRotationSettings'],
	ScaleOverLifetime: ['各軸の拡縮を寿命で変更します。', '開始・終了のScale、Curve、Loop、Easingを設定します。0では該当軸が潰れます。', 'ParticleScaleSettings'],
	ShapeOverLifetime: ['粒子の描画形状のParameterを寿命で変更します。', '使用形状とParameter名を合わせます。対応するPrimitive経路と分割数の負荷も確認します。', 'ParticleShapeSettings'],
	SizeOverLifetime: ['粒子の大きさを寿命で変えます。', '開始値と終了値、CurveまたはEasingを指定します。Textureの透明度とは別です。', 'ParticleSizeSettings'],
	SpiralMovement: ['らせん状の運動を作ります。', '半径、周回数、開始角、逆回転、粒子ごとの角度差を設定します。', 'ParticleSpiralSettings'],
	ForceField: ['SceneのFieldまたはModule内の力場で粒子を動かします。', 'Scene Fieldの利用時はLayerMaskを一致させます。個別設定時は種類・中心・軸・範囲・減衰を指定します。', 'ParticleForceFieldSettings'],
	VectorField: ['3D方向場へ速度を近づけます。', 'SceneのVectorFieldとLayerを合わせ、強さと追従度を指定します。', 'ParticleVectorFieldSettings'],
	SplineMovement: ['Splineの弧長に沿って粒子を移動させます。', 'Splineを参照し、速度、開始距離、周回半径、距離ごとの回転数、接線姿勢を設定します。', 'ParticleSplineMovementSettings'],
	Collision: ['移動した粒子の衝突と応答を計算します。', 'Scene Collider、平面、Heightmap、Camera Depthを選び、粒子半径・反発・摩擦と反射／停止／消滅を設定します。', 'ParticleCollisionSettings'],
};

const emitterNotes = {

	Point: '一点から指定方向へ発生します。', Box: '箱の中や選んだ面から発生します。幅・高さ・奥行きを指定します。',
	Sphere: '球の範囲から発生します。半径を指定します。', Hemisphere: '半球の範囲から発生します。向きと半径を確認します。',
	Torus: '輪状の立体から発生します。大半径と太さを指定します。', Circle: '円周や円の範囲から発生します。角度・順番・初速方向を設定します。',
	Cone: '円錐状に発生します。根元の半径と広がり角を調整します。', Rect: '矩形や選んだ辺から発生します。幅・高さを指定します。',
	Cone2D: '2Dの扇状に発生します。3DのConeとは座標空間が異なります。',
};

// UIの値とAPIの説明を同じ辞書から対応付ける
function describeWidget(field) {

	const value = field.value.replace(/^&/, '').split('.').at(-1).replace(/\(.*$/, '');
	return describeProperty(value) || names[value] || Object.entries(names).find(([name]) => name.toLowerCase() === value.toLowerCase())?.[1] ||
		(/有効|使用|を使う|を表示|を描画/.test(field.label) ? `${field.label}かを切り替えます。対象の機能を使用する場合に有効にします。` :
			/解像度|幅|高さ|サイズ/.test(field.label) ? `${field.label}を指定します。ゲーム画像、入力Texture、形状のどの寸法を編集しているかを確認します。` :
			/色|Color/.test(field.label) ? `${field.label}を指定します。色の乗算とAlphaの変化を確認します。` :
			/秒|時間|間隔|遅延/.test(field.label) ? `${field.label}を指定します。基準となるゲーム時間とUnscaled時間を区別します。` :
			`${field.label}の編集欄です。同じ節の操作手順と、対応する型の設定を確認して変更します。`);
}

export function referencePages(data, api, coverage) {

	for (const tool of data.tools) {
		const [guide, menu, summary] = toolGuides[tool.name] || [];
		if (!guide) throw new Error(`Toolの説明がありません: ${tool.name}`);
		const fields = data.ui.filter(field => field.file.startsWith(path.posix.dirname(tool.source) + '/'));
		const target = `/reference/tools/${slug(tool.name)}`;
		save(`reference/tools/${slug(tool.name)}.md`, tool.label,
			`**開き方：${menu}**\n\n${summary}\n\n## 操作手順\n\n[${tool.label}の使い方](/manual/${guide})を参照してください。\n\n## 設定欄\n\n` +
			(fields.length ? table(['項目', '説明'], [...new Map(fields.map(field => [field.label, field])).values()].map(field => [field.label, describeWidget(field)])) : '表示中の対象と診断結果を確認します。\n') +
			'\n## 保存と切替\n\n保存するAsset・Project設定と、Previewや実行値を区別します。保存ボタンがある場合は変更を確定して保存し、未保存確認では保存・破棄・キャンセルを選びます。\n');
		coverage.tools.push({ id: tool.name, page: target });
	}
	const gpuSource = read('Project/Engine/Core/Rendering/Particle/ParticleGPUProgram.cpp');
	const gpu = new Set(Array.from(gpuSource.matchAll(/\{\s*"([^"]+)",\s*ParticleGPUOperation::/g), match => match[1]).concat(['CustomData', 'CustomShaderParameter']));
	const gpuOnlySource = walk(path.join(root, 'Project/Engine/Core/Rendering/Particle')).filter(file => file.endsWith('.cpp'))
		.map(file => fs.readFileSync(file, 'utf8')).find(source => source.includes('bool Engine::IsParticleModuleGPUOnly')) || '';
	const gpuOnlyBody = gpuOnlySource.slice(gpuOnlySource.indexOf('bool Engine::IsParticleModuleGPUOnly')).split('}')[0];
	const gpuOnly = new Set(Array.from(gpuOnlyBody.matchAll(/"([^"]+)"/g), match => match[1]));
	let moduleText = 'ParticleSystemのPhaseから追加します。現在選択したCPU／GPUで使用できるModuleだけが追加候補に表示されます。\n\n';
	moduleText += table(['Module', 'CPU', 'GPU', '用途'], data.modules.map(module => [`[${modules[module.id] || module.id}](#${slug(module.id)})`, gpuOnly.has(module.id) ? '対象外' : '対応', gpu.has(module.id) ? '対応' : '対象外', moduleDetails[module.id]?.[0] || '']));
	for (const module of data.modules) {
		const detail = moduleDetails[module.id];
		if (!detail) throw new Error(`Moduleの説明がありません: ${module.id}`);
		const type = api.find(type => type.name === detail[2]);
		moduleText += `\n## ${modules[module.id] || module.id} {#${slug(module.id)}}\n\n${detail[0]}\n\n${detail[1]}\n\n`;
		if (type) moduleText += `C#設定型：[${type.name}](/api/${slug(type.id.replace(/^T:NEMEngine\./, ''))})。\n\n`;
		coverage.modules.push({ id: module.id, page: `/reference/particle-modules#${slug(module.id)}` });
	}
	save('reference/particle-modules.md', 'Particle Module一覧', moduleText);
	save('reference/particle-emitters.md', 'Emitter形状', 'Emitterの形状は発生位置と初速の決め方です。描画する粒子の形状とは別です。\n\n' +
		table(['形状', '設定と用途'], data.emitters.map(shape => [shape, emitterNotes[shape] || ''])) +
		'\nGPU SourceでMeshやTextureの表面を使用する場合は、解析的Emitter形状とは取得経路が異なります。[Particleの使い方](/manual/particles)を確認してください。\n');
	let fieldsText = 'Editorの各編集欄を、実装上の画面単位で一覧化しています。値の条件・保存先は、それぞれの機能ページを優先して確認してください。動的なリストの要素は設定数に応じて増減します。\n\n';
	const byFile = new Map();
	for (const field of data.ui) {
		if (!byFile.has(field.file)) byFile.set(field.file, []);
		byFile.get(field.file).push(field);
	}
	for (const [source, fields] of byFile) {
		const title = path.posix.basename(source, '.cpp').replace(/InspectorDrawer$|Drawer$|Tool$|Panel$/, '');
		fieldsText += `## ${title}\n\n${table(['項目', '入力形式', '説明'], fields.map(field => [field.label, widgetName(field.widget), describeWidget(field)]))}\n`;
	}
	save('reference/editor-fields.md', 'Editorの設定欄一覧', fieldsText);
	coverage.ui = data.ui.map(field => ({ id: `${field.file}:${field.line}:${field.label}`, page: '/reference/editor-fields' }));
	const parameterNotes = {
		threshold: 'Bloomとして取り出す明るさの閾値です。', softKnee: 'Bloom閾値付近のなだらかさです。', scatter: 'Bloomの広がりの度合いです。',
		intensity: '効果の強度倍率です。', tint: '効果へ掛けるRGB色です。', density: 'Light Shaftの空気密度です。大きいと全体が霞みます。',
		anisotropy: '光の前方散乱の偏りです。', maxDistance: '効果を計算する距離の上限です。', sampleCount: '積分するサンプル数です。GPU負荷にも影響します。',
		edgeThreshold: 'FXAAで輪郭として扱う明暗差の閾値です。', strength: '補正や効果の強さです。',
	};
	let renderParameters = 'Parameter名はC#で指定する名前と同じです。Materialごとの定義値を掲載します。RenderPasses側に保存した上書き値は別に優先されます。\n\n';
	const materials = walk(path.join(root, 'Project/Engine/Assets/Rendering/Builtin/PostProcess')).filter(file => file.endsWith('.material.json'));
	for (const file of materials) {
		const material = JSON.parse(fs.readFileSync(file, 'utf8'));
		const parameters = material.parameters || {};
		if (!Object.keys(parameters).length) continue;
		renderParameters += `## ${material.name || path.basename(file)}\n\n${table(['Parameter名', '定義値', '意味'], Object.entries(parameters).map(([name, value]) => [name, JSON.stringify(value), parameterNotes[name] || names[name] || `このShaderが公開する${name}の設定です。入力・出力の接続と合わせて調整します。`]))}\n`;
	}
	save('reference/render-parameters.md', '標準PostProcessのParameter', renderParameters);
	const revision = JSON.parse(fs.readFileSync(path.join(output, 'revision.json'), 'utf8'));
	save('manual/coverage.md', '掲載一覧・対応版',
		`対象コミット：\`${revision.commit}\`。記録日：${revision.date}。作業ツリー内の現行ソースを照合しています。コミット後の未確定変更を含む場合があります。\n\n` +
		'## 掲載対象\n\n' + table(['種類', '件数'], [['Inspector Component', data.editors.length], ['Component登録', data.components.length], ['Tool', data.tools.length], ['Particle Module', data.modules.length], ['Emitter形状', data.emitters.length], ['C#公開型', api.length], ['掲載したC#宣言', coverage.api.length], ['C# Component公開プロパティ', coverage.fields.length], ['固定ラベルの編集欄', data.ui.length]]) +
		'\n[Component一覧](/components/) · [C# API一覧](/api/) · [Module一覧](/reference/particle-modules) · [Editorの設定欄](/reference/editor-fields)\n\n' +
		'## Tool一覧\n\n' + table(['Tool', '開き方', '説明'], data.tools.map(tool => [`[${tool.label}](/reference/tools/${slug(tool.name)})`, toolGuides[tool.name][1], toolGuides[tool.name][2]])) +
		'\n## 内部用の登録\n\n' + table(['登録名', '理由'], data.components.filter(component => component.exposure === 'InternalOnly').map(component => [component.registryName, component.reason || '保存・実行用の内部状態またはBufferです。直接追加する利用者向けComponentではありません。'])) +
		'\n## 確認範囲\n\nAPI宣言とComponent対応はソースから抽出します。コード例は別のManaged Projectでコンパイルします。描画品質・全パラメータ組み合わせの目視確認を、自動検証の成功だけで保証するものではありません。Editor画像は撮影した版の操作例です。\n\n' +
		'現在登録されていない旧Effect Asset編集・AnimationController・Volumeなどを、現行の操作として掲載しません。この作業ツリーには照明ベイク・GI事前計算のTool登録もありません。\n');
}

function widgetName(name) {

	if (/Checkbox/.test(name)) return 'チェック';
	if (/ColorEdit/.test(name)) return '色';
	if (/AssetReference/.test(name)) return 'Asset参照';
	if (/EnumCombo/.test(name)) return '選択';
	if (/LayerMask/.test(name)) return 'Layer集合';
	if (/Int/.test(name)) return '整数';
	if (/Float|Slider/.test(name)) return '数値';
	if (/Vector/.test(name)) return 'Vector';
	return '入力';
}
