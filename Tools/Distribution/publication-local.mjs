import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createReadStream} from 'node:fs';
import {copyFile, mkdir, open, readFile, realpath, rename, rm, stat, writeFile} from 'node:fs/promises';
import {randomUUID, createHash} from 'node:crypto';
import {isArtifactKey} from './publication-worker.mjs';

export const testBaseURL = 'https://distribution.invalid/channel';

// ローカル検証では本番の認証・配信先を使用しない
export async function createLocalPublicationClient(directory) {
  const owner = fileURLToPath(new URL('../../LocalWork/Verification/', import.meta.url));
  const root = path.resolve(directory);
  const inside = (parent, child) => {
    const relative = path.relative(parent, child);
    return relative && !relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative);
  };
  if (!inside(owner, root)) throw new Error('テスト配布先はLocalWork/Verificationの配下に指定してください');
  await mkdir(root, {recursive:true});
  if (!inside(await realpath(owner), await realpath(root))) throw new Error('テスト配布先の実体が範囲外にあります');
  const file = key => {
    if (key !== 'catalog.json' && !isArtifactKey(key)) throw new Error('テスト配布キーが不正です');
    return path.join(root, key);
  };
  async function catalog() {
    try {
      const data = await readFile(file('catalog.json'));
      return {body:data, bytes:data.length, etag:'"' + createHash('sha256').update(data).digest('hex') + '"'};
    } catch (error) { if (error.code === 'ENOENT') return null; throw error; }
  }
  async function head(key) {
    try { return {bytes:(await stat(file(key))).size}; }
    catch (error) { if (error.code === 'ENOENT') return null; throw error; }
  }
  return {
    head,
    async get(key) {
      if (key === 'catalog.json') return catalog();
      const info = await head(key);
      return info ? {...info, body:createReadStream(file(key))} : null;
    },
    async put(key, body, options) {
      if (key !== 'catalog.json') throw new Error('テスト用配布一覧のキーが不正です');
      const lockPath = path.join(root, '.catalog.lock');
      const lock = await open(lockPath, 'wx').catch(error => {
        if (error.code === 'EEXIST') throw new Error('別のテスト配布操作が進行中です');
        throw error;
      });
      const temporary = path.join(root, randomUUID() + '.writing');
      try {
        const current = await catalog();
        if ((current?.etag || null) !== options.etag) throw new Error('配布一覧が更新されました 一覧を再取得してください');
        await writeFile(temporary, body);
        await rename(temporary, file(key));
        return (await catalog()).etag;
      } finally {
        await lock.close();
        await rm(lockPath, {force:true});
        await rm(temporary, {force:true});
      }
    },
    async upload(key, source, sha256, progress) {
      const target = file(key);
      await mkdir(path.dirname(target), {recursive:true});
      const temporary = target + '.' + randomUUID() + '.partial';
      try {
        await copyFile(source, temporary);
        const hash = createHash('sha256');
        for await (const chunk of createReadStream(temporary)) hash.update(chunk);
        if (hash.digest('hex') !== sha256) throw new Error('検証中にファイルが変更されました');
        await rename(temporary, target);
        const bytes = (await stat(target)).size;
        progress?.(bytes, bytes);
      } finally { await rm(temporary, {force:true}); }
    },
    async remove(key) {
      if (!key.startsWith('releases/')) throw new Error('削除するエンジンZIPの指定が不正です');
      await rm(file(key), {force:true});
    }
  };
}
