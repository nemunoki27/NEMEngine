import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp, mkdir, writeFile, readFile, rm} from 'node:fs/promises';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {validateRequest, updateContentCatalog, publishContent} from './content.mjs';
import {requireVersion, requireBaseURL} from './publication.mjs';
import {managePublication, isArtifactKey} from './publication-worker.mjs';

const runProcess = promisify(execFile);
function execute(...args) {
  const result = runProcess(...args);
  result.child.stdin.end();
  return result;
}
const baseURL = 'https://example.invalid/channel';
const request = {schemaVersion:1, action:'publish', kind:'sample', id:'sample-test', name:'昼夜のサンプル',
  description:'朝から夜まで', version:'1.0.0', engineVersion:'0.1.9', sourceDirectory:path.resolve('fixture'), baseUrl:baseURL};
const packageInfo = {id:'package', sha256:'a'.repeat(64), url:baseURL + '/project.zip', bytes:128, unpackedBytes:256};
const startupScene = '11111111111111111111111111111111';

async function writeSampleSource(source, sceneDirectory = 'GameAssets/Scenes') {
  await mkdir(path.join(source, sceneDirectory), {recursive:true});
  const files = {
    'template.json':JSON.stringify({schemaVersion:1, id:request.id, kind:'Sample', startupScene}),
    [sceneDirectory + '/日本語.scene.json']:'{"Entities":[]}',
    [sceneDirectory + '/日本語.scene.json.meta']:JSON.stringify({schemaVersion:2, guid:startupScene, type:'Scene'}),
    '.gitignore':'Library/\nSaved/\n',
    '.gitattributes':'* text=auto\n'
  };
  for (const [name, content] of Object.entries(files)) await writeFile(path.join(source, name), content);
  return files;
}

function inspectSample(source, outputArchive, infoFile) {
  return execute('powershell.exe', ['-NoProfile', '-File', fileURLToPath(new URL('ContentArchive.ps1', import.meta.url)),
    '-SourceDirectory', source, '-OutputArchive', outputArchive, '-InfoFile', infoFile], {windowsHide:true});
}

test('公開先と版番号と保存先を検証する', () => {
  validateRequest(request);
  validateRequest({...request, kind:'asset', archivePath:path.resolve('fixture.zip'), fileName:'サンプル素材.zip'});
  for (const version of ['01.1.0', '1.0', '../1.0.0', '', null]) assert.throws(() => requireVersion(version));
  for (const url of ['http://example.invalid', 'https://user:secret@example.invalid', 'https://example.invalid/?token=x'])
    assert.throws(() => requireBaseURL(url));
  for (const fileName of ['../file.zip', 'a\\file.zip', 'CON.zip', 'name.exe', 'x:stream.zip'])
    assert.throws(() => validateRequest({...request, kind:'asset', archivePath:path.resolve('fixture.zip'), fileName}));
});

test('サンプル更新は同じ版の内容を上書きせず他の配布情報を維持する', () => {
  const original = {schemaVersion:1, engines:[{version:'0.1.9'}], assets:[{id:'asset'}], launcher:{version:'0.1.10'}};
  const first = updateContentCatalog(original, request, packageInfo);
  assert.equal(first.samples[0].name, request.name);
  assert.deepEqual(first.assets, original.assets);
  assert.deepEqual(first.launcher, original.launcher);
  assert.equal(original.samples, undefined);
  assert.throws(() => updateContentCatalog(first, request, {...packageInfo, sha256:'b'.repeat(64)}));
  assert.throws(() => updateContentCatalog(original, {...request, engineVersion:'0.1.8'}, packageInfo));
  const next = updateContentCatalog(first, {...request, version:'1.0.1'}, {...packageInfo, sha256:'b'.repeat(64)});
  assert.equal(next.samples.length, 2);
  assert.equal(updateContentCatalog(next, {...request, action:'withdraw'}).samples.length, 0);
});

test('サンプルとアセットの配布パスだけを受け付ける', () => {
  const hash = 'a'.repeat(64);
  assert.equal(isArtifactKey(`samples/test/1.0.0/0.1.9/${hash}/project.zip`), true);
  assert.equal(isArtifactKey(`assets/test/1.0.0/${hash}/asset.zip`), true);
  for (const key of ['assets/test/../../secret.zip', `assets/test/1.0.0/${hash}/state.json`, 'manage/upload', 'catalog.json'])
    assert.equal(isArtifactKey(key), false);
});

test('公開管理は開発者認証と一覧更新の条件を必須にする', async () => {
  const previous = globalThis.fetch;
  const env = {PUBLISHER_USER_ID:'owner', DISTRIBUTION:{put:async (key, body, options) => {
    assert.equal(key, 'catalog.json');
    assert.equal(options.onlyIf.get('if-match'), '"old"');
    return null;
  }}};
  try {
    globalThis.fetch = async () => Response.json({success:true, result:{id:'owner'}});
    const url = baseURL + '/manage/catalog';
    assert.equal((await managePublication(new Request(url, {method:'PUT'}), env, 'catalog')).status, 404);
    const headers = {Authorization:'Bearer test'};
    assert.equal((await managePublication(new Request(url, {method:'PUT', headers}), env, 'catalog')).status, 428);
    headers['If-Match'] = '"old"';
    const body = JSON.stringify({schemaVersion:1, engines:[]});
    assert.equal((await managePublication(new Request(url, {method:'PUT', headers, body}), env, 'catalog')).status, 412);
    globalThis.fetch = async () => Response.json({success:true, result:{id:'another'}});
    assert.equal((await managePublication(new Request(url, {method:'PUT', headers, body}), env, 'catalog')).status, 404);
  } finally { globalThis.fetch = previous; }
});

test('一覧の管理読込はHTTP圧縮に依存せず保存元のETagを返す', async () => {
  const previous = globalThis.fetch;
  const catalog = {schemaVersion:1, engines:[]};
  const env = {PUBLISHER_USER_ID:'owner', DISTRIBUTION:{get:async key => {
    assert.equal(key, 'catalog.json');
    return {json:async () => catalog, httpEtag:'"stored"'};
  }}};
  try {
    globalThis.fetch = async () => Response.json({success:true, result:{id:'owner'}});
    const url = baseURL + '/manage/catalog';
    assert.equal((await managePublication(new Request(url), env, 'catalog')).status, 404);
    const response = await managePublication(new Request(url, {headers:{Authorization:'Bearer test'}}), env, 'catalog');
    assert.deepEqual(await response.json(), {catalog, etag:'"stored"'});
    assert.equal(response.headers.get('cache-control'), 'no-store');
  } finally { globalThis.fetch = previous; }
});

test('アップロードを完了してから一覧を更新し、失敗時は公開しない', async () => {
  const repository = fileURLToPath(new URL('../../', import.meta.url));
  const owner = path.join(repository, 'Generated/DistributionContent');
  await mkdir(owner, {recursive:true});
  const root = await mkdtemp(path.join(owner, 'test-'));
  const source = path.join(root, 'source');
  await writeSampleSource(source);
  const operations = [];
  const client = {
    async get(key) { assert.equal(key, 'catalog.json'); return {body:JSON.stringify({schemaVersion:1, engines:[{version:'0.1.9'}]}), etag:'"old"'}; },
    async head() { return null; },
    async upload(key, file) { assert.ok((await readFile(file)).length > 0); operations.push('upload'); },
    async put(key, body, options) {
      assert.equal(key, 'catalog.json'); assert.equal(options.etag, '"old"');
      const next = JSON.parse(body); assert.ok(next.samples[0].package.unpackedBytes > 0); operations.push('catalog');
    }
  };
  try {
    await publishContent({...request, sourceDirectory:source}, client);
    assert.deepEqual(operations, ['upload', 'catalog']);
    operations.length = 0;
    await assert.rejects(() => publishContent({...request, sourceDirectory:source}, {...client, async upload() { throw new Error('転送失敗'); }}));
    assert.deepEqual(operations, []);
    const controller = new AbortController();
    await assert.rejects(() => publishContent({...request, sourceDirectory:source},
      {...client, async upload() { controller.abort(); }}, controller.signal), {name:'AbortError'});
    assert.deepEqual(operations, []);
  } finally { await rm(root, {recursive:true, force:true}); }
});

test('GameAssetsとPackagesの開始シーンと共通Git設定を保存できる', async () => {
  const owner = fileURLToPath(new URL('../../Generated/DistributionContent/', import.meta.url));
  await mkdir(owner, {recursive:true});
  const root = await mkdtemp(path.join(owner, 'sample-valid-'));
  try {
    for (const [index, directory] of ['GameAssets/Scenes', 'Packages/Stage/Scenes'].entries()) {
      const source = path.join(root, 'source-' + index);
      const files = await writeSampleSource(source, directory);
      const infoFile = path.join(root, 'info-' + index + '.json');
      await inspectSample(source, path.join(root, 'sample-' + index + '.zip'), infoFile);
      const info = JSON.parse(await readFile(infoFile, 'utf8'));
      assert.equal(info.files, Object.keys(files).length);
      assert.equal(info.unpackedBytes, Object.values(files).reduce((total, text) => total + Buffer.byteLength(text), 0));
    }
  } finally { await rm(root, {recursive:true, force:true}); }
});

test('開始シーンGUIDと本体とmetaが不整合なサンプルを公開前に拒否する', async () => {
  const owner = fileURLToPath(new URL('../../Generated/DistributionContent/', import.meta.url));
  await mkdir(owner, {recursive:true});
  const root = await mkdtemp(path.join(owner, 'sample-invalid-'));
  const cases = [
    async source => writeFile(path.join(source, 'template.json'), JSON.stringify({schemaVersion:1, id:request.id, kind:'Sample'})),
    async source => writeFile(path.join(source, 'template.json'), JSON.stringify({schemaVersion:1, id:request.id, kind:'Sample', startupScene:'invalid'})),
    async source => writeFile(path.join(source, 'template.json'), JSON.stringify({schemaVersion:1, id:request.id, kind:'Sample', startupScene:'0'.repeat(32)})),
    async source => rm(path.join(source, 'GameAssets/Scenes/日本語.scene.json.meta')),
    async source => rm(path.join(source, 'GameAssets/Scenes/日本語.scene.json')),
    async source => writeFile(path.join(source, 'GameAssets/Scenes/日本語.scene.json.meta'), JSON.stringify({guid:'2'.repeat(32)})),
    async source => {
      const directory = path.join(source, 'Packages/Duplicate');
      await mkdir(directory, {recursive:true});
      await writeFile(path.join(directory, 'Duplicate.scene.json'), '{"Entities":[]}');
      await writeFile(path.join(directory, 'Duplicate.scene.json.meta'), JSON.stringify({guid:startupScene}));
    }
  ];
  try {
    for (const [index, invalidate] of cases.entries()) {
      const source = path.join(root, 'source-' + index);
      await writeSampleSource(source);
      await invalidate(source);
      const archive = path.join(root, 'bad-' + index + '.zip');
      await assert.rejects(() => inspectSample(source, archive, path.join(root, 'info-' + index + '.json')));
      await assert.rejects(() => readFile(archive), {code:'ENOENT'});
      await assert.rejects(() => readFile(archive + '.writing'), {code:'ENOENT'});
    }
  } finally { await rm(root, {recursive:true, force:true}); }
});

test('ZIP内の階層逸脱と大文字小文字の重複を公開前に拒否する', async () => {
  const owner = fileURLToPath(new URL('../../Generated/DistributionContent/', import.meta.url));
  await mkdir(owner, {recursive:true});
  const root = await mkdtemp(path.join(owner, 'zip-test-'));
  try {
    const script = path.join(root, 'fixtures.ps1');
    await writeFile(script, `Add-Type -AssemblyName System.IO.Compression.FileSystem\nAdd-Type -AssemblyName System.IO.Compression\n$zip=[IO.Compression.ZipFile]::Open($args[0],[IO.Compression.ZipArchiveMode]::Create)\ntry{foreach($name in @('../escape.txt','A.txt','a.txt')){$entry=$zip.CreateEntry($name);$stream=$entry.Open();$stream.WriteByte(1);$stream.Dispose()}}finally{$zip.Dispose()}\n`);
    const archive = path.join(root, 'bad.zip');
    await execute('powershell.exe', ['-NoProfile', '-File', script, archive], {windowsHide:true});
    await assert.rejects(() => execute('powershell.exe', ['-NoProfile', '-File', fileURLToPath(new URL('ContentArchive.ps1', import.meta.url)),
      '-ArchivePath', archive, '-InfoFile', path.join(root, 'info.json')], {windowsHide:true}));
  } finally { await rm(root, {recursive:true, force:true}); }
});
