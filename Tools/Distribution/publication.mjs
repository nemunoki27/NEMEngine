import {createReadStream} from 'node:fs';
import {readFile, stat} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {fileURLToPath} from 'node:url';
import {Transform} from 'node:stream';
import {finished} from 'node:stream/promises';
import {S3Client, GetObjectCommand, HeadObjectCommand, PutObjectCommand, DeleteObjectCommand,
  CreateMultipartUploadCommand, UploadPartCommand, CompleteMultipartUploadCommand, AbortMultipartUploadCommand} from '@aws-sdk/client-s3';
import {requireBaseURL} from './distribution-validation.mjs';
export {requireVersion, requireID, requireBaseURL} from './distribution-validation.mjs';

const execute = promisify(execFile);
const directory = fileURLToPath(new URL('.', import.meta.url));

export async function hashStream(stream, signal) {
  const hash = createHash('sha256');
  for await (const block of stream) { signal?.throwIfAborted(); hash.update(block); }
  return hash.digest('hex');
}
export const hashFile = (file, signal) => hashStream(createReadStream(file, {signal}), signal);

// 転送した内容を照合し、取消時もZIPのハンドルを閉じてから戻る
async function uploadPart(file, offset, length, hash, action) {
  const source = createReadStream(file, {start:offset, end:offset + length - 1});
  const body = new Transform({transform(chunk, encoding, callback) { hash.update(chunk); callback(null, chunk); }});
  const sourceClosed = finished(source).catch(() => {});
  const bodyClosed = finished(body).catch(() => {});
  source.on('error', error => body.destroy(error));
  source.pipe(body);
  try { return await action(body); }
  finally { source.destroy(); body.destroy(); await Promise.all([sourceClosed, bodyClosed]); }
}

// 公開操作で使う認証を配布ファイルへ保存しない
async function readAuthentication() {
  try {
    const {stdout} = await execute(process.execPath, [fileURLToPath(new URL('node_modules/wrangler/bin/wrangler.js', import.meta.url)),
      'auth', 'token', '--json'], {cwd:directory, windowsHide:true, maxBuffer:1024 * 1024});
    const auth = JSON.parse(stdout);
    if (auth.token) return {Authorization:'Bearer ' + auth.token};
    if (auth.type === 'api_key') return {'X-Auth-Key':auth.key, 'X-Auth-Email':auth.email};
  } catch { }
  throw new Error('Cloudflareの認証を確認してください Tools/Distributionでnpx wrangler loginを実行してください');
}

export async function createPublicationClient(options = {}) {
  const signal = options.signal;
  const config = JSON.parse(await readFile(new URL('wrangler.jsonc', import.meta.url), 'utf8'));
  const account = process.env.CLOUDFLARE_ACCOUNT_ID || config.account_id;
  const bucket = process.env.NEM_DISTRIBUTION_BUCKET || config.r2_buckets?.[0]?.bucket_name;
  if (!/^[a-f0-9]{32}$/.test(account ?? '') || !bucket) throw new Error('配布用アカウントとR2バケットを設定してください');
  const accessKeyId = process.env.R2_ACCESS_KEY_ID;
  const secretAccessKey = process.env.R2_SECRET_ACCESS_KEY;
  if (accessKeyId && secretAccessKey) {
    const client = new S3Client({region:'auto', endpoint:`https://${account}.r2.cloudflarestorage.com`, credentials:{accessKeyId, secretAccessKey}});
    return {
      async get(key) {
        try {
          const result = await client.send(new GetObjectCommand({Bucket:bucket, Key:key}), {abortSignal:signal});
          return {body:result.Body, etag:result.ETag, bytes:result.ContentLength, metadata:result.Metadata};
        } catch (error) { if (error.$metadata?.httpStatusCode === 404) return null; throw error; }
      },
      async head(key) {
        try {
          const result = await client.send(new HeadObjectCommand({Bucket:bucket, Key:key}), {abortSignal:signal});
          return {etag:result.ETag, bytes:result.ContentLength, metadata:result.Metadata};
        } catch (error) { if (error.$metadata?.httpStatusCode === 404) return null; throw error; }
      },
      async put(key, body, options = {}) {
        const result = await client.send(new PutObjectCommand({Bucket:bucket, Key:key, Body:body,
          ContentType:options.contentType || 'application/octet-stream', CacheControl:options.cacheControl,
          ...(options.etag ? {IfMatch:options.etag} : options.create ? {IfNoneMatch:'*'} : {})}),
          key === 'catalog.json' ? {} : {abortSignal:signal});
        return result.ETag;
      },
      async upload(key, file, sha256, progress) {
        const bytes = (await stat(file)).size;
        const opened = await client.send(new CreateMultipartUploadCommand({Bucket:bucket, Key:key,
          ContentType:key.endsWith('.zip') ? 'application/zip' : 'application/octet-stream', Metadata:{sha256}}));
        const upload = {Bucket:bucket, Key:key, UploadId:opened.UploadId};
        const hash = createHash('sha256');
        const parts = [];
        try {
          const blockSize = 32 * 1024 * 1024;
          for (let offset = 0; offset < bytes; offset += blockSize) {
            signal?.throwIfAborted();
            const length = Math.min(blockSize, bytes - offset);
            const part = parts.length + 1;
            const result = await uploadPart(file, offset, length, hash, body => client.send(new UploadPartCommand({
              ...upload, PartNumber:part, ContentLength:length, Body:body}), {abortSignal:signal}));
            parts.push({ETag:result.ETag, PartNumber:part});
            progress?.(offset + length, bytes);
          }
          signal?.throwIfAborted();
          if (hash.digest('hex') !== sha256) throw new Error('アップロード中にファイルが変更されました 公開を中止します');
          await client.send(new CompleteMultipartUploadCommand({...upload, MultipartUpload:{Parts:parts}}), {abortSignal:signal});
        } catch (error) {
          try { await client.send(new AbortMultipartUploadCommand(upload)); } catch { }
          throw error;
        }
      },
      async remove(key) { await client.send(new DeleteObjectCommand({Bucket:bucket, Key:key})); }
    };
  }
  const channel = options.baseURL ? options.baseURL : JSON.parse(await readFile(new URL('../../Generated/Distribution/channel.json', import.meta.url), 'utf8')).baseUrl;
  const baseURL = requireBaseURL(channel);
  if (new URL(baseURL).origin !== config.vars?.PUBLISHER_ORIGIN) throw new Error('公開先が配布管理サーバーと一致しません');
  const authentication = await readAuthentication();
  const endpoint = `https://api.cloudflare.com/client/v4/accounts/${account}/r2/buckets/${encodeURIComponent(bucket)}/objects/`;
  async function request(key, options = {}) {
    const response = await fetch(endpoint + key.split('/').map(encodeURIComponent).join('/'), {
      signal, ...options, headers:{...authentication, 'Accept-Encoding':'identity', ...options.headers}, ...(options.body ? {duplex:'half'} : {})});
    if (response.status === 404) return null;
    if (!response.ok) {
      if (response.status === 412) throw new Error('配布一覧が他の操作で更新されました 一覧を再取得して公開し直してください');
      throw new Error(`Cloudflareの配布操作に失敗しました HTTP ${response.status}`);
    }
    return response;
  }
  async function manage(operation, options = {}) {
    const response = await fetch(baseURL + '/manage/' + operation, {
      signal, ...options, headers:{...authentication, ...options.headers}, ...(options.body ? {duplex:'half'} : {})});
    if (!response.ok) {
      if (response.status === 412) throw new Error('配布一覧が他の操作で更新されました 一覧を再取得して公開し直してください');
      throw new Error(`配布管理へ接続できません HTTP ${response.status}`);
    }
    return response;
  }
  return {
    async get(key) {
      if (key === 'catalog.json') {
        // 転送時の圧縮で弱いETagへ変わるため、保存元のETagを本文で受け取る
        const result = await (await manage('catalog')).json();
        return result.catalog ? {body:JSON.stringify(result.catalog), etag:result.etag} : null;
      }
      const response = await request(key);
      return response ? {body:response.body, etag:response.headers.get('etag'), bytes:Number(response.headers.get('content-length'))} : null;
    },
    async head(key) {
      const response = await request(key);
      if (!response) return null;
      await response.body.cancel();
      return {etag:response.headers.get('etag'), bytes:Number(response.headers.get('content-length'))};
    },
    async put(key, body, options = {}) {
      if (key === 'catalog.json') {
        const response = await manage('catalog', {method:'PUT', body, signal:undefined, headers:{'Content-Type':'application/json',
          ...(options.etag ? {'If-Match':options.etag} : options.create ? {'If-None-Match':'*'} : {})}});
        return (await response.json()).etag;
      }
      const response = await request(key, {method:'PUT', body, headers:{'Content-Type':options.contentType || 'application/octet-stream',
        ...(options.cacheControl ? {'Cache-Control':options.cacheControl} : {}),
        ...(options.etag ? {'If-Match':options.etag} : options.create ? {'If-None-Match':'*'} : {})}});
      if (!response) throw new Error('配布先のバケットが見つかりません');
      const data = await response.json();
      if (!data.success) throw new Error('配布ファイルを保存できません');
      return data.result?.etag;
    },
    async upload(key, file, sha256, progress) {
      const bytes = (await stat(file)).size;
      const query = new URLSearchParams({key, action:'create'});
      const start = await manage('upload?' + query, {method:'POST', signal:undefined, headers:{'X-Content-SHA256':sha256}});
      query.set('uploadID', (await start.json()).uploadID);
      const parts = [];
      const hash = createHash('sha256');
      try {
        const blockSize = 32 * 1024 * 1024;
        for (let offset = 0; offset < bytes; offset += blockSize) {
          signal?.throwIfAborted();
          const length = Math.min(blockSize, bytes - offset);
          query.set('action', 'part');
          query.set('part', String(parts.length + 1));
          const result = await uploadPart(file, offset, length, hash, async body => {
            const response = await manage('upload?' + query, {method:'PUT', body, headers:{'Content-Length':String(length)}});
            return response.json();
          });
          parts.push(result);
          progress?.(offset + length, bytes);
        }
        signal?.throwIfAborted();
        if (hash.digest('hex') !== sha256) throw new Error('アップロード中にファイルが変更されました 公開を中止します');
        query.set('action', 'complete');
        const response = await manage('upload?' + query, {method:'POST', body:JSON.stringify(parts)});
        if ((await response.json()).bytes !== bytes) throw new Error('アップロード後のサイズが一致しません');
      } catch (error) {
        query.set('action', 'abort');
        try { await manage('upload?' + query, {method:'DELETE', signal:undefined}); } catch { }
        throw error;
      }
    },
    async remove(key) { await request(key, {method:'DELETE'}); }
  };
}

export async function readCatalog(client) {
  const current = await client.get('catalog.json');
  if (!current) return {catalog:{schemaVersion:1, engines:[]}, etag:null};
  const catalog = JSON.parse(await new Response(current.body).text());
  if (catalog.schemaVersion !== 1 || !Array.isArray(catalog.engines)) throw new Error('配布一覧の形式が不正です');
  return {catalog, etag:current.etag};
}

export async function writeCatalog(client, catalog, etag) {
  return client.put('catalog.json', JSON.stringify(catalog, null, 2) + '\n',
    {contentType:'application/json', cacheControl:'no-cache', etag, create:!etag});
}

export async function uploadArchive(client, file, key, expected, options = {}) {
  const bytes = (await stat(file)).size;
  const sha256 = options.verifiedHash || await hashFile(file, options.signal);
  if (expected && (bytes !== expected.bytes || sha256 !== expected.sha256)) throw new Error('配布情報とファイルが一致しません');
  const existing = await client.head(key);
  if (existing) {
    if (existing.bytes !== bytes) throw new Error('公開済みの配布物とサイズが一致しません');
    const remoteHash = existing.metadata?.sha256 || await hashStream((await client.get(key)).body, options.signal);
    if (remoteHash !== sha256) throw new Error('公開済みの配布物と内容が一致しません');
    console.log('配置済み: ' + key);
    return;
  }
  console.log('アップロード中: ' + key);
  let previous = -1;
  await client.upload(key, file, sha256, (completed, total) => {
    const percentage = Math.floor(completed * 100 / total);
    if (percentage !== previous) { console.log(`アップロード中: ${percentage}%`); previous = percentage; }
  });
}
