import {readFile} from 'node:fs/promises';
import {manageRelease} from './engine-release.mjs';

const args = process.argv.slice(2);
if (args.length < 2 || args.length > 3) throw new Error('使用方法: node publish.mjs release-0.1.0.json HTTPS配布URL [NEMLauncherSetup.exe]');
const [releasePath, baseURL, setupPath] = args;
const {version, notes} = JSON.parse(await readFile(releasePath, 'utf8'));
await manageRelease({action:'publish', version, notes, releasePath, baseURL}, undefined, {setupPath});
console.log('公開しました: ' + baseURL + '/catalog.json');
