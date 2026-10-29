import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { root, output, read, json, write, walk } from './paths.mjs';

// 入れ子の引数と文字列を分けて呼出し全体を読む
function argumentsAt(source, start) {

	const argumentsList = [];
	let depth = 0, quote = '', escaped = false, first = start + 1;
	for (let index = start; index < source.length; ++index) {
		const character = source[index];
		if (quote) {
			if (escaped) escaped = false;
			else if (character === '\\') escaped = true;
			else if (character === quote) quote = '';
			continue;
		}
		if (character === '"' || character === "'") { quote = character; continue; }
		if ('([{'.includes(character)) ++depth;
		if (')]}'.includes(character)) --depth;
		if ((character === ',' && depth === 1) || depth === 0) {
			argumentsList.push(source.slice(first, index).trim());
			first = index + 1;
		}
		if (depth === 0) return argumentsList;
	}
	throw new Error('UI呼出しの終端が見つかりません');
}

export function widgets(source) {

	const result = [];
	const pattern = /\b(?:MyGUI|ImGui|InspectorDrawerCommon)::((?:Drag|ColorEdit|EnumCombo|Checkbox|Input|Slider|AssetReference|DrawCheckbox|DrawLayerMask|DrawAssetReference)\w*)(?:<[^>\n]+>)?\s*\(/g;
	for (const match of source.matchAll(pattern)) {
		const args = argumentsAt(source, match.index + match[0].lastIndexOf('('));
		if (!/^"[^"\n]*"$/.test(args[0] || '')) continue;
		const label = args[0].slice(1, -1).split('##')[0];
		if (!label.trim()) continue;
		const value = (args[1] || '').replace(/^&/, '').trim();
		result.push({ label, value, widget: match[1], options: args.slice(2).join(', '), line: source.slice(0, match.index).split('\n').length });
	}
	return result;
}

// 登録と現在のソースから掲載台帳を作る
export function inventory() {

	const manifest = json('Project/Engine/Core/Scripting/Managed/Bindings/ComponentManifest.json');
	const editorFiles = walk(path.join(root, 'Project/Engine/Editor')).filter(file => /\.(cpp|h)$/.test(file));
	const files = editorFiles.map(file => ({ file: path.relative(root, file).replaceAll('\\', '/'), source: fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n') }));
	const ui = files.flatMap(({ file, source }) => widgets(source).map(field => ({ ...field, file })));
	const registry = read('Project/Engine/Editor/UI/Inspectors/Builtin/BuiltinComponentEditorRegistration.cpp');
	const editors = Array.from(registry.matchAll(/MakeComponentEditorDescriptor<\s*(\w+)\s*>\s*\(\s*"([^"]+)"\s*,\s*(?:"([^"]+)"|ScriptComponent::kTypeName)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"/g), match => ({
		className: match[1], label: match[2], name: match[3] || 'Script', category: match[4], source: `Project/Engine/Editor/${match[5]}`,
	}));
	const toolRegistration = read('Project/Engine/Editor/Tools/Builtin/BuiltinEditorTools.cpp');
	const tools = Array.from(toolRegistration.matchAll(/RegisterBuiltinEditorTool<(\w+)>\(\);/g), match => {
		const header = files.find(file => file.file.endsWith(`/${match[1]}.h`));
		const descriptor = /ToolDescriptor descriptor_\s*\{([\s\S]*?)\};/.exec(header?.source || '')?.[1] || '';
		const strings = Array.from(descriptor.matchAll(/"([^"]*)"/g), m => m[1]);
		return { name: match[1], id: strings[0], label: strings[1], menu: strings[2], source: header?.file };
	});
	const modules = Array.from(read('Project/Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.cpp')
		.matchAll(/Register\("([^"]+)",\s*&CreateParticleModule<(\w+)>/g), match => ({ id: match[1], name: match[2] }));
	const emitters = Array.from(read('Project/Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.cpp')
		.matchAll(/Register\(ParticleEmitterShape::(\w+)/g), match => match[1]);
	const sources = [...files.filter(file => /Inspectors|Tools\/Builtin|Panels\/Builtin/.test(file.file)),
		...walk(path.join(root, 'Project/Engine/Core')).filter(file => /\.(h|cpp)$/.test(file)).map(file => ({
			file: path.relative(root, file).replaceAll('\\', '/'), source: fs.readFileSync(file, 'utf8'),
		})),
		...walk(path.join(root, 'Project/Engine/Managed/NEM.ScriptCore')).filter(file => /\.cs$/.test(file)).map(file => ({
			file: path.relative(root, file).replaceAll('\\', '/'), source: fs.readFileSync(file, 'utf8'),
		})),
		...walk(path.join(root, 'Project/Engine/Assets/Rendering/Builtin/PostProcess')).filter(file => file.endsWith('.json')).map(file => ({
			file: path.relative(root, file).replaceAll('\\', '/'), source: fs.readFileSync(file, 'utf8'),
		})),
		{ file: 'Project/Engine/Core/Scripting/Managed/Bindings/ComponentManifest.json', source: JSON.stringify(manifest) }];
	const hashes = Object.fromEntries(sources.map(({ file, source }) => [file, crypto.createHash('sha256').update(source.replace(/\r\n/g, '\n')).digest('hex')]));
	const result = { components: manifest.components, editors, tools, modules, emitters, ui, hashes };
	write(path.join(output, 'inventory.json'), JSON.stringify(result, null, 2));
	let commit = 'working-tree';
	try { commit = execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8', windowsHide: true }).trim(); } catch { }
	write(path.join(output, 'revision.json'), JSON.stringify({ commit, date: new Date().toLocaleDateString('sv-SE', { timeZone: 'Asia/Tokyo' }) }, null, 2));
	console.log(`掲載対象: Editor Component ${editors.length} / 登録 ${manifest.components.length} / Tool ${tools.length} / Module ${modules.length} / UI項目 ${ui.length}`);
	return result;
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.join(root, 'Tools/Documentation/inventory.mjs')) inventory();
