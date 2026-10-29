import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { root, website, output, read, write, walk, formatDocumentation } from './paths.mjs';
import { inventory } from './inventory.mjs';
import { components, describeProperty } from './component-notes.mjs';
import { componentDetails, componentExamples, componentReferenceTypes } from './component-details.mjs';
import { hiddenTypes, hiddenReason, guideFor, memberDescription, modules } from './api-notes.mjs';
import { referencePages } from './reference-pages.mjs';

export const slug = value => value.replace(/^T:NEMEngine\./, '').replace(/([A-Z]+)([A-Z][a-z])/g, '$1-$2').replace(/([a-z\d])([A-Z])/g, '$1-$2')
	.replace(/[^\w-]+/g, '-').replace(/^-|-$/g, '').toLowerCase();
const anchor = value => `member-${crypto.createHash('sha1').update(value).digest('hex').slice(0, 12)}`;
const cell = value => String(value ?? '').replaceAll('|', '\\|').replaceAll('\n', ' ').split(/(`[^`]+`)/g)
	.map((part, index) => index % 2 ? part : part.replaceAll('<', '&lt;').replaceAll('>', '&gt;')).join('');
const table = (headers, rows) => `| ${headers.join(' | ')} |\n| ${headers.map(() => '---').join(' | ')} |\n${rows.map(row => `| ${row.map(cell).join(' | ')} |`).join('\n')}\n`;
const generatedPages = new Set();
const save = (relative, text) => { generatedPages.add(relative); write(path.join(website, relative), formatDocumentation(text)); };
const page = (title, text) => `---\ntitle: ${JSON.stringify(title)}\n---\n\n# ${title.replaceAll('<', '&lt;').replaceAll('>', '&gt;')}\n\n${text}\n`;

// 記事と照合できる最新の公開宣言を取得する
export function generate({ metadata = true } = {}) {

	if (metadata) execFileSync('dotnet', ['run', ...(fs.existsSync(path.join(output, 'Metadata/obj/project.assets.json')) ? ['--no-restore'] : []), '--project', path.join(root, 'Tools/Documentation/Metadata/DocumentationMetadata.csproj'), '--', root, path.join(output, 'api.json')],
		{ cwd: root, encoding: 'utf8', stdio: 'inherit', windowsHide: true });
	const data = inventory();
	const api = JSON.parse(fs.readFileSync(path.join(output, 'api.json'), 'utf8'));
	const published = api.filter(type => !hiddenTypes.test(type.name));
	const unresolved = [];
	const typeLinks = new Map(published.map(type => [type.fullName, `/api/${slug(type.id)}`]));
	const coverage = { components: [], componentSettings: [], componentAPI: [], tools: [], modules: [], api: [], excluded: [], fields: [] };
	const propertyLinks = new Map();
	const resolvedBindings = new Map();
	for (const component of data.components) {
		if (!published.some(type => type.name === component.managedType)) continue;
		for (const property of component.properties || []) {
			const resolved = resolveBinding(published, component, property);
			resolvedBindings.set(`${component.registryName}.${property.managedName}`, resolved);
			propertyLinks.set(`${resolved.type.name}.${resolved.member.name}`, { component, property });
		}
	}
	const apiLink = name => {
		const type = published.find(type => type.name === name);
		return type ? `[${name}](${typeLinks.get(type.fullName)})` : `\`${name}\``;
	};
	const nativeDefinitions = readDefinitions();
	const componentEntries = data.editors.concat(data.components.filter(component => component.exposure === 'GeneratedBinding' &&
		!data.editors.some(editor => editor.name === component.registryName)).map(component => ({ name: component.registryName,
		className: '', label: component.registryName, category: 'C# API', source: `Project/${component.nativeHeader}` })));
	for (const editor of componentEntries) {
		const note = components[editor.name];
		if (!note) throw new Error(`Componentの説明がありません: ${editor.name}`);
		const details = componentDetails[editor.name];
		if (!details?.length) throw new Error(`Inspectorの詳説がありません: ${editor.name}`);
		const component = data.components.find(item => item.registryName === editor.name);
		const managed = published.find(type => type.name === (component?.managedType || (editor.name === 'Script' ? 'MonoBehaviour' : '')));
		const target = `/components/${slug(editor.name)}`;
		const fields = component?.properties || [];
		const source = read(editor.source);
		const drawerPart = functionBody(source, editor.className);
		const rows = fields.map(property => {
			const resolved = resolvedBindings.get(`${editor.name}.${property.managedName}`);
			if (!resolved) throw new Error(`公開C#設定がありません: ${editor.name}.${property.managedName}`);
			const description = describeProperty(property.nativeMember);
			if (!description) unresolved.push(`${editor.name}.${property.managedName}`);
			const match = data.ui.find(field => field.file === editor.source &&
				field.value.replace(/^draft\./, '') === property.nativeMember && drawerPart.includes(field.label));
			const defaults = nativeDefault(nativeDefinitions, component.nativeType, property.nativeMember);
			coverage.fields.push({ id: `${editor.name}.${property.managedName}`, page: target, described: Boolean(description),
				managedPath: resolved.path, member: resolved.member.id });
			return [match?.label || resolved.path, `\`${resolved.path}\``, `\`${resolved.member.returnType.replaceAll('NEMEngine.', '')}\``,
				resolved.member.access === 'ReadOnly' ? '読取専用' : '読取・変更', defaults || '追加時の設定を参照', description || ''];
		});
		let body = `${note[0]}\n\n## 設定する手順\n\n1. ${note[1]}\n2. SceneViewまたはGameViewで結果を確認します。\n3. 編集中のSceneまたはPrefabを保存します。\n\n`;
		body += `## 項目の説明\n\n${fields.length ? table(['項目', 'C#名', '型', '操作', '定義時の初期値', '意味'], rows) : '複雑な設定は下記の専用APIとInspectorの編集欄を使用します。\n'}\n`;
		body += '## Inspectorの設定と使い方\n\n「Editor / C#」は両方から変更できる設定、「Editorのみ」は公開C# APIのない編集操作です。「Editor表示のみ」は計算結果や統計などの確認欄です。「C#のみ」は、このInspectorに編集欄のない操作です。\n\n';
		body += table(['設定・操作', '編集場所', 'C#の入口', '意味・使い方'], details.map((detail, index) => {
			coverage.componentSettings.push({ id: `${editor.name}.${index}`, component: editor.name, page: target, ...detail });
			return [detail.label, detail.mode, detail.api.map(name => `\`${name}\``).join(' / ') || '—', detail.description];
		}));
		body += '\n';
		body += `初期値はEngineのComponent定義時の値で、C++表記を含みます。C#へ貼り付ける実行例ではありません。既存Sceneの保存値や既定Assetがある場合はそちらが優先されます。数値の編集範囲はInspectorで制限される場合があります。\n\n`;
		body += `## C#から使う\n\n${managed ? `操作型は${apiLink(managed.name)}です。` : '名前・親子関係などの基本操作はGameObjectとTransformを使用します。'} `;
		body += 'GameObjectから取得する基本操作は[GameObjectの操作](/manual/entity)を参照してください。以下には基本プロパティだけでなく、公開関数・追加の設定API・継承した共通操作も掲載します。\n\n';
		const example = componentExamples[editor.name];
		if (example) body += `### 実用例：${example[1]}\n\n${example[2]}\n\n<<< ../../../Tools/Documentation/Samples/${example[0]}\n\n`;
		const references = componentReferenceTypes[editor.name] || [];
		if (references.length) {
			body += '### 設定用・関連するC#型\n\n';
			for (const name of references) {
				if (!published.some(type => type.name === name)) throw new Error(`設定用のC#型がありません: ${name}`);
				body += `- ${apiLink(name)}\n`;
			}
			body += '\n';
		}
		if (managed) {
			const settingsTypes = published.filter(type => type.fullName.startsWith(`${managed.fullName}.`));
			const enumTypes = published.filter(type => type.kind === 'Enum' && [managed, ...settingsTypes].some(owner => owner.members.some(member =>
				[member.returnType, ...member.parameters.map(parameter => parameter.type)].some(name => name?.replace(/\[\]|\?/g, '') === type.fullName))));
			if (enumTypes.length) body += '### 選択値（Enum）\n\n' + table(['型・各値の詳説', '使える値'], enumTypes.map(type => [apiLink(type.name),
				type.members.length > 20 ? `${type.members.length}件。型のページで全候補を確認できます。` : type.members.map(member => `\`${member.name}\``).join(' / ')])) + '\n';
			const apiTypes = [managed, ...settingsTypes];
			let parent = published.find(type => type.fullName === managed.baseType);
			while (parent) {
				apiTypes.push(parent);
				parent = published.find(type => type.fullName === parent.baseType);
			}
			for (const type of apiTypes) {
				const inherited = type !== managed && !settingsTypes.includes(type);
				body += `### ${inherited ? '継承する共通操作：' : ''}${type.fullName.replace(/^NEMEngine\./, '')}\n\n`;
				body += table(['宣言・詳説', '種類・操作', '役割'], type.members.map(member => {
					const binding = propertyLinks.get(`${type.name}.${member.name}`);
					const kind = member.kind === 'Method' ? '関数' : member.kind === 'Property' ? 'プロパティ' : 'Field';
					const access = member.kind === 'Method' ? '' : member.access === 'ReadOnly' ? '（読取専用）' : '（読取・変更）';
					coverage.componentAPI.push({ id: `${editor.name}:${member.id}`, component: editor.name, member: member.id, page: target });
					return [`[\`${member.signature}\`](/api/${slug(type.id)}#${anchor(member.id)})`, kind + access,
						memberDescription(type, member, binding?.property)];
				}));
				body += '\n';
			}
		}
		body += `## 使用時の注意\n\n${note[2]}\n\nPlay中の変更は実行用Worldへ反映します。Scene編集のUndoと実行中の値の変更は別です。保存方法は[編集と実行時変更](/manual/reference-guide)を参照してください。\n\n## 関連項目\n\n- [機能の使い方](/manual/${note[3]})\n- [Component一覧](/components/)\n- [C# API一覧](/api/)\n`;
		save(`components/${slug(editor.name)}.md`, page(`${editor.name} Component`, body));
		coverage.components.push({ id: editor.name, page: target, source: editor.source, managedType: managed?.fullName || null });
	}
	for (const type of api.filter(type => hiddenTypes.test(type.name))) coverage.excluded.push({ id: type.id, reason: hiddenReason });
	for (const type of published) {
		const typeSlug = slug(type.id);
		const component = data.components.find(component => component.managedType === type.name);
		const note = component && components[component.registryName];
		const intro = note?.[0] || typeSummary(type);
		let body = `${intro}\n\n[使い方と前提条件](/manual/${guideFor(type)}) · [C# API一覧](/api/)\n\n`;
		body += `## 型の情報\n\n- **名前空間**：\`NEMEngine\`\n- **型**：\`${type.fullName}\`（${type.kind}）\n`;
		if (type.baseType && !['object', 'System.Enum', 'System.ValueType'].includes(type.baseType)) body += `- **継承元**：${typeLinks.has(type.baseType) ? `[${type.baseType}](${typeLinks.get(type.baseType)})` : `\`${type.baseType}\``}\n`;
		if (note) body += `- **Inspector**：[${component.registryName}](/components/${slug(component.registryName)})\n`;
		body += '\n';
		if (type.kind === 'Enum') {
			body += `## 選択値\n\n${table(['名前', '定義値', '使い方'], type.members.map(member => [
				`\`${member.name}\``, member.initializer || '宣言順に割当', memberDescription(type, member)]))}\n`;
			for (const member of type.members) coverage.api.push({ id: member.id, page: `/api/${typeSlug}`, described: true });
		} else {
			for (const member of type.members) {
				const binding = propertyLinks.get(`${type.name}.${member.name}`);
				const description = memberDescription(type, member, binding?.property);
				if (!description) unresolved.push(`${type.fullName}.${member.name}`);
				const memberAnchor = anchor(member.id);
				body += `## ${member.name === '.ctor' ? type.name : member.name} {#${memberAnchor}}\n\n\`\`\`csharp\n${member.signature}\n\`\`\`\n\n${(description || '説明を確認中です。').replaceAll('<', '&lt;').replaceAll('>', '&gt;')}\n\n`;
				if (member.parameters.length) body += `${table(['引数', '型', '既定値'], member.parameters.map(parameter => [
					`\`${parameter.name}\``, `\`${parameter.refKind === 'None' ? '' : `${parameter.refKind} `}${parameter.type}\``, parameter.optional ? parameter.defaultValue ?? 'null' : '必須']))}\n`;
				if (member.kind === 'Property' || member.kind === 'Field') body += `操作：**${member.access === 'ReadOnly' ? '読取専用' : member.access === 'ReadWrite' ? '読取・変更' : '値の参照・設定'}**。\n\n`;
				if (member.initializer) body += `定義値：\`${member.initializer.replaceAll('`', '')}\`。\n\n`;
				if (binding && components[binding.component.registryName]) body += `対応するInspector：[${binding.component.registryName}](/components/${slug(binding.component.registryName)})。\n\n`;
				if (member.returnType && member.returnType !== 'void' && member.kind === 'Method') body += `戻り値：\`${member.returnType}\`。\n\n`;
				coverage.api.push({ id: member.id, page: `/api/${typeSlug}#${memberAnchor}`, described: Boolean(description) });
			}
		}
		body += '## 実行時の扱い\n\nNativeと接続する操作は、Scriptが実行される有効なWorld内で使用します。破棄したGameObjectやComponentへのアクセスは避けてください。設定Snapshotは取得・編集・反映を1組で扱います。\n';
		save(`api/${typeSlug}.md`, page(type.fullName.replace(/^NEMEngine\./, ''), body));
		coverage.api.push({ id: type.id, page: `/api/${typeSlug}`, described: true });
	}
	referencePages(data, published, coverage);
	write(path.join(output, 'unresolved.json'), JSON.stringify([...new Set(unresolved)], null, 2));
	write(path.join(output, 'coverage.json'), JSON.stringify(coverage, null, 2));
	write(path.join(output, 'source-hashes.json'), JSON.stringify(data.hashes, null, 2));
	generateIndexes(data, published, coverage, typeLinks);
	const extraComponents = coverage.components.filter(component => !data.editors.some(editor => editor.name === component.id));
	if (extraComponents.length) {
		const index = path.join(website, 'components/index.md');
		write(index, formatDocumentation(fs.readFileSync(index, 'utf8') + '\n## C#から操作する追加Component\n\n' + extraComponents.map(component =>
			`- [${component.id}](${component.page})：${components[component.id][0]}`).join('\n') + '\n'));
	}
	// 公開型の削除や改名で古い参照ページを残さない
	for (const directory of ['api', 'components']) {
		for (const file of walk(path.join(website, directory)).filter(file => file.endsWith('.md'))) {
			const relative = path.relative(website, file).replaceAll('\\', '/');
			if (!generatedPages.has(relative)) fs.unlinkSync(file);
		}
	}
	console.log(`生成: API ${published.length}型 / ${coverage.api.length}宣言 / 未説明 ${new Set(unresolved).size}`);
}

// 接続用の平坦な名前を、実際の公開Accessorへ対応付ける
function resolveBinding(api, component, property) {

	let type = api.find(type => type.name === component.managedType);
	let memberPath = property.managedName;
	if (!type.members.some(member => member.name === memberPath)) {
		if (component.registryName === 'ParticleSystem' && memberPath === 'RandomSeedValue') memberPath = 'randomSeed';
		if (component.registryName === 'CameraController') {
			for (const [prefix, access] of [['FollowLookAtFollow', 'FollowLookAt.Follow'], ['FollowLookAtLookAt', 'FollowLookAt.LookAt'], ['Follow', 'Follow'], ['LookAt', 'LookAt']]) {
				if (memberPath.startsWith(prefix)) { memberPath = `${access}.${memberPath.slice(prefix.length)}`; break; }
			}
		}
		if (component.registryName === 'PrimitiveRenderer') {
			const prefix = ['CrossPlane', 'Hemisphere', 'Cylinder', 'Sphere', 'Plane', 'Ring', 'Cube'].find(name => memberPath.startsWith(name));
			if (prefix) memberPath = `${prefix}.${memberPath.slice(prefix.length)}`;
		}
	}
	const parts = memberPath.split('.');
	for (let index = 0; index < parts.length; ++index) {
		const member = type?.members.find(member => member.name === parts[index]);
		if (!member) throw new Error(`公開C# APIを解決できません: ${component.managedType}.${memberPath}`);
		if (index === parts.length - 1) return { path: memberPath, type, member };
		type = api.find(type => type.fullName === member.returnType || type.fullName === `NEMEngine.${member.returnType}`);
	}
}

// 関数内だけを切り出して別Componentの欄を混ぜない
function functionBody(source, name) {

	const begin = source.indexOf(`${name}::DrawFields(`);
	if (begin < 0) return source;
	const body = source.indexOf('{', begin);
	let depth = 1, quote = '', escaped = false;
	for (let index = body + 1; index < source.length; ++index) {
		const character = source[index];
		if (quote) { if (escaped) escaped = false; else if (character === '\\') escaped = true; else if (character === quote) quote = ''; continue; }
		if (character === '"') { quote = character; continue; }
		if (character === '{') ++depth;
		if (character === '}' && --depth === 0) return source.slice(body, index);
	}
	return source.slice(body);
}

function readDefinitions() {

	const definitions = new Map();
	for (const file of walk(path.join(root, 'Project/Engine/Core/World/Components')).filter(file => file.endsWith('.h'))
		.concat(walk(path.join(root, 'Project/Engine/Core/Rendering/RenderFeatures')).filter(file => file.endsWith('.h')))) {
		const source = fs.readFileSync(file, 'utf8');
		for (const match of source.matchAll(/struct\s+(\w+)(?:\s+final)?\s*\{([\s\S]*?)\n\t\};/g)) {
			const fields = new Map();
			for (const field of match[2].matchAll(/^\s*(?!static\b)([\w:<>]+)\s+(\w+)\s*(?:=\s*([^;\n]+)|(\{[^;\n]*\}))?\s*;/gm)) {
				fields.set(field[2], { type: field[1], value: field[3] || field[4] || '' });
			}
			definitions.set(match[1], fields);
		}
	}
	return definitions;
}

function nativeDefault(definitions, type, member) {

	for (const part of member.split('.')) {
		const field = definitions.get(type)?.get(part);
		if (!field) return '';
		type = field.type;
		if (part === member.split('.').at(-1)) return field.value ? `\`${field.value.replace(/^Engine::/, '')}\`` : '';
	}
	return '';
}

function typeSummary(type) {

	if (type.name === 'GameObject') return 'Hierarchyに表示されるGameObjectをC#から操作する参照です。[GameObjectの生成・取得・親子付け・破棄](/manual/entity)を参照してください。';
	if (type.kind === 'Enum') return `${type.name}で選ぶ動作・状態の一覧です。C#では列挙名を使って指定します。`;
	if (/Settings$|Accessor$/.test(type.name)) return `${type.name}は、[${guideLabel(guideFor(type))}](/manual/${guideFor(type)})の個別設定をまとめる型です。取得元の設定から値を編集し、所有するComponentへ反映します。`;
	if (/Attribute$/.test(type.name)) return `${type.name}は、Scriptの型や保存Fieldに付ける属性です。[属性の一覧・対象・記述例](/manual/script-attributes)で、保存・Inspector表示・実行順・識別の役割を確認してください。`;
	if (/^Vector|Quaternion|Color|Mathf|Easing|Random/.test(type.name)) return `${type.name}はゲーム中の計算に使用する型です。ベクトル、色、回転を取り違えず、角度の単位と座標空間を確認してください。`;
	return type.summary && !/====|class|opaque|ABI/.test(type.summary) ? type.summary.replaceAll('<', '&lt;').replaceAll('>', '&gt;') : `${type.name}は[${guideLabel(guideFor(type))}](/manual/${guideFor(type)})で使用する型です。下記にゲームコードから利用できる宣言を掲載します。`;
}

const guideLabel = id => ({ particles: 'Particle', camera: 'Camera', rendering: '描画', input: '入力', audio: '音声', physics: 'Physics', ui: 'UI', animation: 'Animation', assets: 'Asset', data: 'CSV・JSON', materials: 'Material', text: 'Font・文字', scene: 'Sceneと親子関係', entity: 'GameObject', scripting: 'C# Script', 'script-attributes': 'SerializeFieldなどの属性', 'post-process': 'RenderPasses', 'project-settings': 'プロジェクト設定' })[id] || id;

function generateIndexes(data, api, coverage, typeLinks) {

	const componentRows = data.editors.map(editor => [`[${editor.name}](/components/${slug(editor.name)})`, editor.category, components[editor.name][0]]);
	save('components/index.md', page('Component一覧', `HierarchyでEntityを選択し、Inspectorの「コンポーネントを追加」から追加します。Transformは初めから付いています。\n\n${table(['Component', '分類', '用途'], componentRows)}\nC#名が異なる例として、AnimationPlayerはAnimator、CollisionはCollisionComponentを使います。`));
	const groups = new Map();
	for (const type of api) {
		const guide = guideFor(type);
		if (!groups.has(guide)) groups.set(guide, []);
		groups.get(guide).push({ text: type.fullName.replace(/^NEMEngine\./, ''), link: typeLinks.get(type.fullName) });
	}
	let apiIndex = '正確な型名・メンバー名はC#コードと一致します。使い始める場合は[スクリプトの書き方](/manual/scripting)から進めてください。\n\n';
	for (const [guide, types] of groups) {
		apiIndex += `## ${guideLabel(guide)} {#${guide}}\n\n`;
		if (guide === 'entity') apiIndex += 'GameObjectの名前、親子関係、Component、生成と破棄を操作します。[使い方と実例](/manual/entity)を確認してください。\n\n';
		if (guide === 'script-attributes') apiIndex += '[すべての属性の用途・対象・記述例](/manual/script-attributes)を確認してください。NativeAssetTypeはEngineのAsset接続用です。\n\n';
		apiIndex += `${types.map(type => `- [${type.text.replaceAll('<', '&lt;').replaceAll('>', '&gt;')}](${type.link})`).join('\n')}\n\n`;
	}
	save('api/index.md', page('C# API一覧', apiIndex));
	const navigation = [
		{ text: 'はじめに', items: [{ text: '導入と必要環境', link: '/guide/start' }, { text: '最初のゲームを作る', link: '/guide/first-game' }, { text: '配布用にビルドする', link: '/manual/build' }] },
		{ text: '基本操作', items: ['editor', 'assets', 'scene', 'project-settings', 'reference-guide'].map(id => ({ text: { editor: 'EditorとPanel', assets: 'Assetの管理', scene: 'Scene・Prefab・親子関係', 'project-settings': 'プロジェクト設定', 'reference-guide': '変数・参照・保存の読み方' }[id], link: `/manual/${id}` })) },
		{ text: '機能の使い方', collapsed: false, items: ['rendering', 'camera', 'materials', 'shader-graph', 'post-process', 'particles', 'animation', 'physics', 'input', 'ui', 'audio', 'text', 'decal', 'data', 'scripting', 'performance'].map(id => ({ text: { 'shader-graph': 'ShaderGraph', decal: 'Decal', performance: '計測と最適化', ...Object.fromEntries([...groups.keys()].map(key => [key, guideLabel(key)])) }[id] || id, link: `/manual/${id}` })) },
		{ text: 'Componentリファレンス', collapsed: true, items: [{ text: 'Component一覧', link: '/components/' }, ...data.editors.map(editor => ({ text: editor.name, link: `/components/${slug(editor.name)}` }))] },
		{ text: 'C# API', collapsed: true, items: [{ text: 'API一覧', link: '/api/' }, ...[...groups.keys()].map(key => ({ text: guideLabel(key), link: ['entity', 'script-attributes'].includes(key) ? `/manual/${key}` : `/api/#${key}` }))] },
		{ text: '補足', items: [{ text: '困ったときは', link: '/manual/troubleshooting' }, { text: '用語とUnityとの違い', link: '/manual/glossary' }, { text: '掲載一覧・対応版', link: '/manual/coverage' }, { text: 'マニュアルの更新', link: '/manual/documentation' }] },
	];
	write(path.join(output, 'navigation.json'), JSON.stringify(navigation, null, 2));
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.join(root, 'Tools/Documentation/generate.mjs')) generate({ metadata: !process.argv.includes('--no-metadata') });
