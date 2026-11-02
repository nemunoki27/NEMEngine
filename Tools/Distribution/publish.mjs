import {createReadStream} from 'node:fs';
import {readFile, stat} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {S3Client, HeadObjectCommand, GetObjectCommand, PutObjectCommand} from '@aws-sdk/client-s3';
import {Upload} from '@aws-sdk/lib-storage';

const args = process.argv.slice(2);
if (args.length < 2 || args.length > 3) throw new Error('使用方法: node publish.mjs release-0.1.0.json HTTPS配布URL [NEMLauncherSetup.exe]');
const [releasePath, baseUrl, setupPath] = args;
const launcherVersion = (await readFile(new URL('./LauncherVersion.props', import.meta.url),'utf8')).match(/<Version>(\d+\.\d+\.\d+)<\/Version>/)?.[1];
if (!launcherVersion) throw new Error('ランチャーの版番号を確認できません');
const origin = new URL(baseUrl);
if (origin.protocol !== 'https:') throw new Error('配布URLはHTTPSで指定してください');
const account = process.env.CLOUDFLARE_ACCOUNT_ID;
const accessKeyId = process.env.R2_ACCESS_KEY_ID;
const secretAccessKey = process.env.R2_SECRET_ACCESS_KEY;
if (!/^[a-f0-9]{32}$/.test(account ?? '') || !accessKeyId || !secretAccessKey)
  throw new Error('公開用PCのCLOUDFLARE_ACCOUNT_ID、R2_ACCESS_KEY_ID、R2_SECRET_ACCESS_KEYを設定してください');
const bucket = process.env.NEM_DISTRIBUTION_BUCKET ?? 'nemengine-distribution';
const client = new S3Client({region:'auto', endpoint:`https://${account}.r2.cloudflarestorage.com`, credentials:{accessKeyId,secretAccessKey}});
const release = JSON.parse(await readFile(releasePath,'utf8'));
if (!/^\d+\.\d+\.\d+$/.test(release.version)) throw new Error('版番号が不正です');
const directory = path.dirname(path.resolve(releasePath));
const url = key => baseUrl.replace(/\/$/,'')+'/'+key;
const digest = async stream => {const hash=createHash('sha256'); for await (const block of stream) hash.update(block); return hash.digest('hex');};
const sha = file => digest(createReadStream(file));
async function head(key) {
  try {return await client.send(new HeadObjectCommand({Bucket:bucket,Key:key}));}
  catch(error) {if (error.$metadata?.httpStatusCode===404) return null; throw error;}
}
async function upload(file,key,expected) {
  const bytes=(await stat(file)).size;
  const hash=await sha(file);
  if (expected && (bytes!==expected.bytes || hash!==expected.sha256)) throw new Error('配布情報とファイルが一致しません: '+file);
  const existing=await head(key);
  if (existing) {
    if (existing.ContentLength!==bytes) throw new Error('同じ版の配布物を上書きできません: '+key);
    let remoteHash=existing.Metadata?.sha256;
    // CLIで配置した初回配布物も実体を照合して再利用する
    if (!remoteHash) {
      const remote=await client.send(new GetObjectCommand({Bucket:bucket,Key:key}));
      remoteHash=await digest(remote.Body);
    }
    if (remoteHash!==hash) throw new Error('同じ版の配布物を上書きできません: '+key);
    console.log('配置済み: '+key);
    return;
  }
  // 大容量ファイルを分割し、失敗した転送は中断する
  const transfer=new Upload({client,params:{Bucket:bucket,Key:key,Body:createReadStream(file),ContentLength:bytes,
    ContentType:key.endsWith('.json')?'application/json':'application/octet-stream',Metadata:{sha256:hash}},
    partSize:32*1024*1024,queueSize:2,leavePartsOnError:false});
  await transfer.done();
  console.log('配置完了: '+key);
}
let catalog={schemaVersion:1,engines:[]};
let catalogETag;
try {
  const response=await client.send(new GetObjectCommand({Bucket:bucket,Key:'catalog.json'}));
  catalog=JSON.parse(await response.Body.transformToString());
  catalogETag=response.ETag;
} catch(error) {if (error.$metadata?.httpStatusCode!==404) throw error;}
if (catalog.schemaVersion!==1) throw new Error('配布一覧の形式が不正です');
const previous=catalog.engines.find(item=>item.version===release.version);
if (previous && (previous.engine.sha256!==release.engine.sha256 || previous.toolchain.sha256!==release.toolchain.sha256 || previous.toolchainVersion!==release.toolchainVersion))
  throw new Error('公開済みの版には新しい番号を付けてください');
const engineKey=`releases/${release.version}/${release.engine.sha256}/engine.zip`;
const toolchainKey=`toolchains/${release.toolchain.id}.zip`;
await upload(path.join(directory,release.engine.id+'.zip'),engineKey,release.engine);
await upload(path.join(directory,release.toolchain.id+'.zip'),toolchainKey,release.toolchain);
release.engine.url=url(engineKey);
release.toolchain.url=url(toolchainKey);
catalog.engines=catalog.engines.filter(item=>item.version!==release.version);
catalog.engines.push(release);
if (setupPath) {
  const setupSha = await sha(setupPath);
  if (catalog.launcher?.version===launcherVersion && catalog.launcher.setup.sha256!==setupSha)
    throw new Error('ランチャーも更新する場合はLauncherVersion.propsの版を上げてください');
  const key=`launcher/${launcherVersion}/${setupSha}/NEMLauncherSetup.exe`;
  await upload(setupPath,key);
  const bytes=(await stat(setupPath)).size;
  catalog.launcher={version:launcherVersion,setup:{id:`nemlauncher-${launcherVersion}-win-x64`,url:url(key),bytes,unpackedBytes:bytes,sha256:setupSha}};
}
// 全配布物の転送後に一覧だけを切り替える
await client.send(new PutObjectCommand({Bucket:bucket,Key:'catalog.json',Body:JSON.stringify(catalog,null,2)+'\n',
  ContentType:'application/json',CacheControl:'no-cache',...(catalogETag?{IfMatch:catalogETag}:{IfNoneMatch:'*'})}));
console.log('公開しました: '+url('catalog.json'));
