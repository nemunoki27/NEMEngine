import {readFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';

const [file, catalogURL, mode] = process.argv.slice(2);
if (!file || !catalogURL || new URL(catalogURL).protocol !== 'https:' ||
    (mode && !['--binaries-only','--catalog-only'].includes(mode)))
  throw new Error('使用方法: node verify-live.mjs catalog.json HTTPS一覧URL [--binaries-only|--catalog-only]');
const catalog = JSON.parse(await readFile(file, 'utf8'));
const packages = catalog.engines.flatMap(release => [release.engine, release.toolchain]);
if (catalog.launcher) packages.push(catalog.launcher.setup);
for (const item of mode === '--catalog-only' ? [] : packages) {
  // 公開URLの実体をメモリへ溜めずに照合する
  const response = await fetch(item.url, {redirect:'error'});
  if (response.status !== 200 || Number(response.headers.get('content-length')) !== item.bytes)
    throw new Error('取得サイズが一致しません: ' + item.id);
  const hash = createHash('sha256');
  let bytes = 0;
  for await (const block of response.body) {
    bytes += block.length;
    if (bytes > item.bytes) throw new Error('取得サイズが上限を超えています: ' + item.id);
    hash.update(block);
  }
  if (bytes !== item.bytes || hash.digest('hex') !== item.sha256)
    throw new Error('SHA-256が一致しません: ' + item.id);
  // 再開取得とサイズ確認を配信先でも確認する
  const head = await fetch(item.url, {method:'HEAD', redirect:'error'});
  const range = await fetch(item.url, {headers:{Range:'bytes=0-1023'}, redirect:'error'});
  if (head.status !== 200 || Number(head.headers.get('content-length')) !== item.bytes ||
      range.status !== 206 || range.headers.get('content-range') !== `bytes 0-1023/${item.bytes}` ||
      (await range.arrayBuffer()).byteLength !== 1024)
    throw new Error('部分取得またはHEADに失敗しました: ' + item.id);
  console.log('検証成功: ' + item.id + ' / SHA-256・サイズ・部分取得');
}
const hidden = await fetch(new URL('/', catalogURL));
if (hidden.status !== 404) throw new Error('配信元のrootが公開されています');
const denied = await fetch(packages[0].url, {method:'POST'});
if (denied.status !== 405) throw new Error('配信先が書き込み要求を拒否していません');
if (mode !== '--binaries-only') {
  const response = await fetch(catalogURL, {redirect:'error'});
  if (response.status !== 200 || !response.headers.get('cache-control')?.includes('no-cache') ||
      JSON.stringify(await response.json()) !== JSON.stringify(catalog))
    throw new Error('公開一覧が一致しません');
}
console.log('公開検証が完了しました');
