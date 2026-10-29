import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import MiniSearch from '../../Docs/Website/node_modules/minisearch/dist/es/index.js';
import { tokenize } from './search.mjs';
import { root, output, write } from './paths.mjs';

const dist = path.join(output, 'dist');
const files = [];
function visit(directory) {

	for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
		const file = path.join(directory, entry.name);
		if (entry.isDirectory()) visit(file);
		else files.push(file);
	}
}
visit(dist);
const html = files.filter(file => file.endsWith('.html'));
const anchors = new Map();
for (const file of html) {
	const source = fs.readFileSync(file, 'utf8');
	anchors.set(file, new Set(Array.from(source.matchAll(/\bid="([^"]+)"/g), match => match[1])));
	if (!/<meta name="robots" content="noindex, nofollow"/.test(source)) throw new Error(`noindexがありません: ${file}`);
	if (/C:[\\/]Users[\\/]|nemunoki2701@|説明を確認中/.test(source)) throw new Error(`公開できない内容: ${file}`);
}
let internalLinks = 0;
for (const file of html) {
	const source = fs.readFileSync(file, 'utf8');
	for (const match of source.matchAll(/<a\b[^>]*\bhref="([^"]*)"/g)) {
		const href = match[1];
		if (/^(?:https?:|mailto:)/.test(href)) continue;
		const [location, fragment] = href.split('#');
		const route = decodeURIComponent(location.split('?')[0]);
		const target = route ? path.resolve(route.startsWith('/') ? dist : path.dirname(file), `.${route.startsWith('/') ? route : '/' + route}`) : file;
		const candidates = [target, `${target}.html`, path.join(target, 'index.html')];
		const resolved = candidates.find(candidate => anchors.has(candidate));
		if (!resolved) {
			if (files.includes(target)) continue;
			throw new Error(`公開物のリンク切れ: ${path.relative(dist, file)} → ${href}`);
		}
		if (fragment && !anchors.get(resolved).has(decodeURIComponent(fragment))) throw new Error(`見出しがありません: ${path.relative(dist, file)} → ${href}`);
		++internalLinks;
	}
}
for (const file of files) {
	if (fs.statSync(file).size > 25 * 1024 * 1024) throw new Error(`Pagesの単一ファイル上限を超えています: ${file}`);
}
if (files.length > 20000) throw new Error('Pagesのファイル数上限を超えています');

// ブラウザーへ配布する索引そのものから検索結果を確認する
const indexFile = files.find(file => /@localSearchIndexroot\..*\.js$/.test(file));
if (!indexFile) throw new Error('ローカル検索の索引がありません');
const { default: serialized } = await import(pathToFileURL(indexFile).href);
const index = MiniSearch.loadJSON(serialized, { fields: ['title', 'titles', 'text'], storeFields: ['title', 'titles'], tokenize });
const queries = ['パーティクル', '当たり判定', 'クリアカラー', 'GameObject', 'GetComponent', 'ParticleSystem', 'RenderPasses', '保存'];
const searchResults = {};
for (const query of queries) {
	const result = index.search(query, { prefix: true, fuzzy: 0.15, boost: { title: 5, titles: 2, text: 1 } });
	if (!result.length) throw new Error(`検索結果がありません: ${query}`);
	searchResults[query] = result.slice(0, 3).map(item => ({ id: item.id, title: item.title }));
}
const bytes = files.reduce((sum, file) => sum + fs.statSync(file).size, 0);
write(path.join(output, 'artifact-verification.json'), JSON.stringify({ date: new Date().toISOString(), files: files.length, pages: html.length, internalLinks,
	bytes, searchResults, browserReview: '未実施: Windows画面操作ツールが現在URLを判定できず停止', editorScreenshots: '未撮影' }, null, 2));
console.log(`公開物検証成功: ${html.length}ページ / ${files.length}ファイル / ${(bytes / 1024 / 1024).toFixed(1)}MB / 日本語・API名検索${queries.length}件`);
