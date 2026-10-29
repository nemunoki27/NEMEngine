import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { root, website, output, walk, write, formatDocumentation } from './paths.mjs';
import { componentDetails, componentExamples } from './component-details.mjs';
import { inventory } from './inventory.mjs';

const failures = [];
const check = (condition, message) => { if (!condition) failures.push(message); };
const load = file => JSON.parse(fs.readFileSync(path.join(output, file), 'utf8'));
const data = inventory();
const coverage = load('coverage.json');
const unresolved = load('unresolved.json');
const api = load('api.json');
check(unresolved.length === 0, `未説明のAPI: ${unresolved.join(', ')}`);

// 登録数だけでなく識別子を照合する
function covers(expected, entries, label) {

	const actual = new Set(entries.map(entry => entry.id));
	for (const id of expected) check(actual.has(id), `${label}の掲載漏れ: ${id}`);
}
covers(data.editors.map(component => component.name), coverage.components, 'Inspector');
covers(data.tools.map(tool => tool.name), coverage.tools, 'Tool');
covers(data.modules.map(module => module.id), coverage.modules, 'Particle Module');
covers(data.components.flatMap(component => (component.properties || []).map(property => `${component.registryName}.${property.managedName}`)), coverage.fields, 'Componentプロパティ');
for (const entry of [...coverage.fields, ...coverage.api]) check(entry.described, `説明がありません: ${entry.id}`);
const declaredMembers = new Set(api.flatMap(type => type.members.map(member => member.id)));
for (const field of coverage.fields) check(field.managedPath && declaredMembers.has(field.member), `公開C#名を確認できません: ${field.id}`);
for (const component of coverage.components) {
	const details = componentDetails[component.id];
	check(details?.length > 0, `Inspectorの詳説がありません: ${component.id}`);
	const own = api.find(type => type.fullName === component.managedType);
	if (!own) { check(false, `ComponentのC#操作型がありません: ${component.id}`); continue; }
	const parents = [];
	let parent = api.find(type => type.fullName === own.baseType);
	while (parent) { parents.push(parent); parent = api.find(type => type.fullName === parent.baseType); }
	const types = [own, ...api.filter(type => type.fullName.startsWith(`${own.fullName}.`)), ...parents];
	covers(types.flatMap(type => type.members.map(member => `${component.id}:${member.id}`)), coverage.componentAPI, `${component.id}のC#操作`);
	const names = new Set([own, ...parents].flatMap(type => type.members.map(member => member.name)));
	for (const detail of details || []) {
		check(detail.description.length > 15, `${component.id}の設定説明が短すぎます: ${detail.label}`);
		for (const name of detail.api) check(names.has(name), `${component.id}の設定が存在しないC#操作を参照: ${name}`);
	}
	const example = componentExamples[component.id];
	if (example) check(fs.existsSync(path.join(root, 'Tools/Documentation/Samples', example[0])), `${component.id}の実用例がありません`);
}
check(coverage.ui.length === data.ui.length, 'UI項目一覧を再生成してください');

const generatedSources = load('source-hashes.json');
const staleSources = [...new Set([...Object.keys(generatedSources), ...Object.keys(data.hashes)])]
	.filter(file => generatedSources[file] !== data.hashes[file]);
check(staleSources.length === 0, '掲載用データが古いためdocs:generateを実行してください');

// 確認前のソース変更を公開へ流さない
const baseline = path.join(root, 'Tools/Documentation/reviewed-sources.json');
if (!fs.existsSync(baseline)) failures.push('ソース確認一覧がありません。READMEの確認手順を参照してください');
else {
	const reviewed = JSON.parse(fs.readFileSync(baseline, 'utf8'));
	const changed = [...new Set([...Object.keys(reviewed), ...Object.keys(data.hashes)])]
		.filter(file => reviewed[file] !== data.hashes[file]);
	check(changed.length === 0, `再確認が必要なソース ${changed.length}件:\n${changed.slice(0, 20).join('\n')}`);
}

const markdown = walk(website).filter(file => file.endsWith('.md') && !file.endsWith('README.md'));
let links = 0, snippets = 0;
const snippetSources = [];
for (const file of markdown) {
	const source = fs.readFileSync(file, 'utf8');
	check(formatDocumentation(source) === source, `説明文にEntity表記が残っています: ${path.relative(website, file)}`);
	check(!/説明を確認中|TODO|TBD|<<<<<<<|>>>>>>>/.test(source), `未完了の記述: ${path.relative(root, file)}`);
	check(!/[A-Z]:[\\/]Users[\\/]|nemunoki2701@|Bearer\s+[A-Za-z0-9_-]{20}/.test(source), `個人パスまたは秘密情報: ${file}`);
	const withoutCode = source.replace(/```[\s\S]*?```/g, '');
	for (const match of withoutCode.matchAll(/\]\(([^)]+)\)|href="([^"]+)"/g)) {
		const url = match[1] || match[2];
		if (/^(?:https?:|mailto:|#)/.test(url)) continue;
		const urlPath = decodeURIComponent(url.split('#')[0]);
		const resolved = urlPath.startsWith('/') ? path.join(website, urlPath) : path.resolve(path.dirname(file), urlPath);
		check([resolved, `${resolved}.md`, path.join(resolved, 'index.md'), path.join(website, 'public', urlPath)]
			.some(candidate => fs.existsSync(candidate) && fs.statSync(candidate).isFile()), `リンク切れ: ${path.relative(website, file)} → ${url}`);
		++links;
	}
	for (const match of source.matchAll(/^<<<\s+(.+)$/gm)) {
		const target = path.resolve(path.dirname(file), match[1].trim());
		check(fs.existsSync(target), `サンプルがありません: ${file} → ${match[1]}`);
		if (fs.existsSync(target)) check(/\/\/[^\n]*[ぁ-んァ-ヶ一-龠]/.test(fs.readFileSync(target, 'utf8')), `サンプルに日本語の処理コメントがありません: ${target}`);
		++snippets;
	}
	if (/[/\\](?:guide|manual)[/\\]/.test(file)) {
		for (const match of source.matchAll(/```csharp\s*\n([\s\S]*?)```/g)) {
			const body = match[1];
			check(/\/\/[^\n]*[ぁ-んァ-ヶ一-龠]/.test(body), `コード例に日本語の処理コメントがありません: ${file}`);
			if (body.includes('[SerializeField]')) snippetSources.push(body);
			else snippetSources.push(`void Check${snippetSources.length}() {\n${body}\n}`);
		}
	}
}

// 埋め込んだ例と本文内の短い例をコンパイルする
if (!process.argv.includes('--no-samples')) {
	const snippetDirectory = path.join(output, 'Samples');
	write(path.join(snippetDirectory, 'ManualSnippets.cs'), `using NEMEngine;\npublic sealed class ManualSnippets : MonoBehaviour {\n${snippetSources.join('\n')}\n}\n`);
	try {
		execFileSync('dotnet', ['build', path.join(root, 'Tools/Documentation/Samples/DocumentationSamples.csproj'), '-c', 'Develop', '--nologo', '-v:minimal'],
			{ cwd: root, stdio: 'inherit', windowsHide: true });
	} catch { failures.push('C#コード例のコンパイルに失敗しました'); }
}
const headers = fs.readFileSync(path.join(website, 'public/_headers'), 'utf8');
check(headers.includes('X-Robots-Tag: noindex') && headers.includes('Referrer-Policy: no-referrer'), '公開用ヘッダーが不足しています');
check(!fs.existsSync(path.join(website, 'public/robots.txt')) || !/Disallow:\s*\//.test(fs.readFileSync(path.join(website, 'public/robots.txt'), 'utf8')), 'noindexを妨げるrobots.txtです');
write(path.join(output, 'verification.json'), JSON.stringify({ date: new Date().toISOString(), pages: markdown.length, links, snippets,
	components: coverage.components.length, componentSettings: coverage.componentSettings.length,
	componentAPI: coverage.componentAPI.length, properties: coverage.fields.length, api: coverage.api.length, failures }, null, 2));
if (failures.length) {
	console.error(failures.join('\n'));
	process.exitCode = 1;
} else console.log(`検証成功: ${markdown.length}ページ / ${links}リンク / Component ${coverage.components.length} / 公開プロパティ ${coverage.fields.length} / C#宣言 ${coverage.api.length}`);
