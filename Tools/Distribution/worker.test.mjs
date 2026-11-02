import test from 'node:test';
import assert from 'node:assert/strict';
import worker from './worker.mjs';

const env = {CHANNEL:'shared-channel', DISTRIBUTION:{ async get(key) {
  return {size:3, body:'abc', httpEtag:'"hash"', writeHttpMetadata() {}};
}}};
test('配布以外のパスを拒否する', async () => {
  for (const path of ['/', '/catalog.json', '/shared-channel/', '/shared-channel/state.json', '/shared-channel/releases/../secret'])
    assert.equal((await worker.fetch(new Request('https://example.invalid'+path), env)).status, 404);
});
test('一覧と配布物を取得できる', async () => {
  const response = await worker.fetch(new Request('https://example.invalid/shared-channel/catalog.json'), env);
  assert.equal(response.status,200);
  assert.equal(await response.text(),'abc');
  assert.equal(response.headers.get('cache-control'),'no-cache');
});
test('書込と一覧表示を提供しない', async () => {
  assert.equal((await worker.fetch(new Request('https://example.invalid/shared-channel/catalog.json',{method:'PUT'}),env)).status,405);
});
test('部分取得のサイズと範囲を返す', async () => {
  const partial = {...env, DISTRIBUTION:{async get() {return {size:100, range:{offset:10,length:3},body:'abc',httpEtag:'"hash"',writeHttpMetadata(){}};}}};
  const response = await worker.fetch(new Request('https://example.invalid/shared-channel/releases/0.1.0/'+'a'.repeat(64)+'/engine.zip',{headers:{Range:'bytes=10-12'}}),partial);
  assert.equal(response.status,206);
  assert.equal(response.headers.get('content-range'),'bytes 10-12/100');
});
test('全体取得にR2の範囲情報があっても200を返す', async () => {
  const whole = {...env, DISTRIBUTION:{async get() {return {size:3,range:{offset:0,length:3},body:'abc',httpEtag:'"hash"',writeHttpMetadata(){}};}}};
  for (const method of ['GET','HEAD']) {
    const response = await worker.fetch(new Request('https://example.invalid/shared-channel/catalog.json',{method}),whole);
    assert.equal(response.status,200);
    assert.equal(response.headers.get('content-range'),null);
    assert.equal(response.headers.get('content-length'),'3');
  }
});

const launcher = {version:'0.1.9', setup:{sha256:'a'.repeat(64),
  url:'https://example.invalid/shared-channel/launcher/0.1.9/'+'a'.repeat(64)+'/NEMLauncherSetup.exe'}};
test('最新版の入口はカタログのSetupへキャッシュせず転送する', async () => {
  const latest = {...env, DISTRIBUTION:{async get(key) {assert.equal(key,'catalog.json'); return {async json(){return {launcher};}};}}};
  for (const method of ['GET','HEAD']) {
    const response = await worker.fetch(new Request('https://example.invalid/shared-channel/launcher/latest/NEMLauncherSetup.exe',{method}),latest);
    assert.equal(response.status,302);
    assert.equal(response.headers.get('location'),launcher.setup.url);
    assert.equal(response.headers.get('cache-control'),'no-store');
    assert.equal(await response.text(),'');
  }
});
test('最新版の入口で外部URLや不正な配布情報へ転送しない', async () => {
  for (const changed of [{...launcher,setup:{...launcher.setup,url:'https://other.invalid/installer.exe'}},
    {...launcher,setup:{...launcher.setup,url:launcher.setup.url+'?other=true'}}, null]) {
    const latest = {...env, DISTRIBUTION:{async get(){return {async json(){return {launcher:changed};}};}}};
    assert.equal((await worker.fetch(new Request('https://example.invalid/shared-channel/launcher/latest/NEMLauncherSetup.exe'),latest)).status,503);
  }
});
