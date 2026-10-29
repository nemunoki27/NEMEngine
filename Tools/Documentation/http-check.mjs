import fs from 'node:fs';
import path from 'node:path';
import { output, website, walk, write } from './paths.mjs';

const origin = new URL(process.argv[2] || 'http://127.0.0.1:4173/');
const local = ['localhost', '127.0.0.1'].includes(origin.hostname);
const routes = walk(website).filter(file => file.endsWith('.md') && !file.endsWith('README.md'))
	.map(file => '/' + path.relative(website, file).replaceAll('\\', '/').replace(/(?:index)?\.md$/, ''));
const failures = [];
const results = [];
let next = 0;

// 実際のURLから全ページと直接アクセス時の応答を確認する
async function worker() {

	while (next < routes.length) {
		const route = routes[next++];
		try {
			const response = await fetch(new URL(route, origin), { signal: AbortSignal.timeout(20000) });
			const html = await response.text();
			if (response.status !== 200 || !/<meta name="robots" content="noindex, nofollow"/.test(html)) {
				failures.push(`${route}: HTTP ${response.status}またはnoindexが不正です`);
			}
			if (!local && !response.headers.get('x-robots-tag')?.includes('noindex')) failures.push(`${route}: X-Robots-Tagがありません`);
			results.push({ route, status: response.status });
		} catch (error) { failures.push(`${route}: ${error.message}`); }
	}
}
await Promise.all(Array.from({ length: 8 }, worker));
const missing = await fetch(new URL('/not-a-documentation-page-404', origin), { signal: AbortSignal.timeout(10000) });
if (missing.status !== 404) failures.push(`不存在ページがHTTP ${missing.status}です`);
const assetRoot = path.join(output, 'dist');
const assets = walk(assetRoot).filter(file => /\.(?:js|css|svg|png|jpg|webp|woff2|json)$/.test(file))
	.map(file => '/' + path.relative(assetRoot, file).replaceAll('\\', '/'));
let nextAsset = 0;
async function assetWorker() {
	while (nextAsset < assets.length) {
		const asset = assets[nextAsset++];
		const response = await fetch(new URL(asset, origin), { signal: AbortSignal.timeout(20000) });
		if (!response.ok || response.headers.get('content-type')?.includes('text/html')) failures.push(`${asset}: HTTP ${response.status}または形式が不正です`);
		await response.arrayBuffer();
	}
}
await Promise.all(Array.from({ length: 8 }, assetWorker));
write(path.join(output, local ? 'http-verification.json' : 'deployment-verification.json'), JSON.stringify({
	date: new Date().toISOString(), origin: origin.href, pages: results.length, assets: assets.length, failures,
}, null, 2));
if (failures.length) {
	console.error(failures.slice(0, 20).join('\n'));
	process.exitCode = 1;
} else console.log(`HTTP検証成功: ${results.length}ページ / 404 / 資源${assets.length}件${local ? '' : ' / 公開ヘッダー'}`);
