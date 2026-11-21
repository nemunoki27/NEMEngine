import test from 'node:test';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {mkdir, mkdtemp, readFile, rm, writeFile} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {manageRelease} from './engine-release.mjs';
import {updateContentCatalog} from './content.mjs';
import {createLocalPublicationClient} from './publication-local.mjs';

const baseURL = 'https://distribution.invalid/channel';
const version = '0.1.2';
function createBucket() {
  const objects = new Map();
  let revision = 0;
  return {
    objects,
    async get(key) { return objects.get(key) || null; },
    async head(key) { return objects.get(key) || null; },
    async upload(key, file, sha256) {
      const body = await readFile(file);
      objects.set(key, {body, bytes:body.length, metadata:{sha256}});
    },
    async put(key, body, options) {
      const previous = objects.get(key);
      assert.equal(previous?.etag || null, options.etag);
      assert.equal(options.create, !previous);
      const etag = '"' + ++revision + '"';
      objects.set(key, {body, etag});
      return etag;
    },
    async remove(key) { objects.delete(key); }
  };
}

async function writePackage(root, id, content) {
  const body = Buffer.from(content);
  await writeFile(path.join(root, id + '.zip'), body);
  return {id, bytes:body.length, unpackedBytes:body.length, sha256:createHash('sha256').update(body).digest('hex')};
}

test('エンジンの新規公開・更新内容の編集・サンプル依存・中断したZIP削除を同じ配布先で確認する', async t => {
  const owner = fileURLToPath(new URL('../../LocalWork/Verification/DeveloperLauncher', import.meta.url));
  await mkdir(owner, {recursive:true});
  const root = await mkdtemp(path.join(owner, 'catalog-'));
  t.after(() => rm(root, {recursive:true, force:true}));
  const releasePath = path.join(root, 'release.json');
  const engine = await writePackage(root, 'nemengine-test', 'engine');
  const toolchain = await writePackage(root, 'dotnet-test', 'toolchain');
  await writeFile(releasePath, JSON.stringify({version, engine, toolchain, minimumLauncherVersion:'0.1.11', toolchainVersion:'10.0.401'}));
  const client = createBucket();
  const run = (action, etag, extra = {}, target = client) => manageRelease({action, etag, version, releasePath, baseURL, notes:'新しい版', ...extra}, target);
  assert.equal((await run('list')).etag, null);
  await assert.rejects(() => run('publish', null, {}, {...client, upload() { throw new Error('転送失敗'); }}), /転送失敗/);
  assert.equal(client.objects.size, 0);
  let result = await run('publish', null);
  assert.equal(result.engines[0].notes, '新しい版');
  assert.equal(client.objects.size, 3);
  await assert.rejects(() => run('publish', result.etag), /公開済み/);
  await assert.rejects(() => run('notes', null), /再取得/);
  await assert.rejects(() => run('delete', null), /再取得/);
  const before = result.engines[0];
  result = await run('notes', result.etag, {notes:'更新内容のみ変更'}, {...client, upload() { assert.fail('ZIPを転送しました'); }});
  assert.deepEqual(result.engines[0], {...before, notes:'更新内容のみ変更'});
  const sample = {action:'publish', kind:'sample', id:'sample', name:'デモ', version:'1.0.0', engineVersion:version};
  const key = 'catalog.json';
  const beforeSample = JSON.parse(client.objects.get(key).body);
  const published = updateContentCatalog(beforeSample, sample, before.engine);
  const withSample = await client.put(key, JSON.stringify(published), {etag:result.etag, create:false});
  await assert.rejects(() => run('delete', withSample), /デモ/);
  assert.equal(client.objects.size, 3);
  let etag = await client.put(key, JSON.stringify(updateContentCatalog(published, {...sample, action:'withdraw'})), {etag:withSample, create:false});
  await assert.rejects(() => run('delete', etag, {}, {...client, remove() { throw new Error('通信中断'); }}), /再試行/);
  result = await run('list');
  assert.deepEqual(result.engines, []);
  assert.deepEqual(result.pendingDeletes, [version]);
  assert.deepEqual(result.retiredVersions, [version]);
  result = await run('cleanup', result.etag);
  assert.deepEqual(result.pendingDeletes, []);
  assert.deepEqual(result.retiredVersions, [version]);
  assert.deepEqual([...client.objects.keys()].sort(), [key, 'toolchains/dotnet-test.zip']);
  await assert.rejects(() => run('publish', result.etag), /再利用/);
  await assert.rejects(() => manageRelease({action:'publish', baseURL, version, notes:'旧CLI', releasePath}, client, {}), /再利用/);
});

test('削除時の競合でカタログの取り下げに失敗したら本体を保持する', async () => {
  const sha256 = 'b'.repeat(64);
  const url = baseURL + '/releases/' + version + '/' + sha256 + '/engine.zip';
  const client = createBucket();
  const etag = await client.put('catalog.json', JSON.stringify({schemaVersion:1, engines:[{version, engine:{url, sha256}}]}),
    {etag:null, create:true});
  client.remove = () => assert.fail('一覧から取り下げる前に削除しました');
  client.put = () => { throw Object.assign(new Error('Precondition failed'), {$metadata:{httpStatusCode:412}}); };
  await assert.rejects(() => manageRelease({action:'delete', baseURL, version, etag}, client), /再取得/);
  assert.equal((await manageRelease({action:'list', baseURL}, client)).engines.length, 1);
});

test('テスト配布先を検証ディレクトリに限定し、古い一覧での書き込みを拒む', async t => {
  const owner = fileURLToPath(new URL('../../LocalWork/Verification/DeveloperLauncher', import.meta.url));
  await mkdir(owner, {recursive:true});
  const root = await mkdtemp(path.join(owner, 'local-'));
  t.after(() => rm(root, {recursive:true, force:true}));
  await assert.rejects(() => createLocalPublicationClient(path.join(owner, '../../..')), /配下/);
  const client = await createLocalPublicationClient(root);
  assert.equal(await client.get('catalog.json'), null);
  const previous = await client.put('catalog.json', '{"schemaVersion":1,"engines":[]}', {etag:null});
  assert.equal((await client.get('catalog.json')).etag, previous);
  await assert.rejects(() => client.put('catalog.json', '{}', {etag:null}), /再取得/);
  await assert.rejects(() => client.remove('catalog.json'), /エンジンZIP/);
  await assert.rejects(() => client.head('../catalog.json'), /キー/);
});
