import {readFile, mkdir, mkdtemp, rm, writeFile, rename, access} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {execFile} from 'node:child_process';
import {promisify} from 'node:util';
import {createPublicationClient, hashFile, readCatalog, writeCatalog, uploadArchive,
  requireVersion, requireID, requireBaseURL} from './publication.mjs';
import {requireText, requireZIPName, validateContentCatalog} from './distribution-validation.mjs';

const runProcess = promisify(execFile);
function execute(...args) {
  const result = runProcess(...args);
  result.child.stdin.end();
  return result;
}
const repository = fileURLToPath(new URL('../../', import.meta.url));

export function validateRequest(request) {
  if (request.schemaVersion !== 1 || !['publish', 'withdraw'].includes(request.action) || !['sample', 'asset'].includes(request.kind))
    throw new Error('配布操作の指定が不正です');
  requireID(request.id);
  if (request.action === 'withdraw') return;
  requireVersion(request.version);
  requireText(request.name, 120, true);
  requireText(request.description ?? '', 4000);
  if (request.description === null) throw new Error('配布内容の説明が不正です');
  if (request.kind === 'sample') {
    requireVersion(request.engineVersion);
    if (!path.isAbsolute(request.sourceDirectory || '')) throw new Error('サンプルの保存元を指定してください');
  } else {
    if (request.recommendedEngineVersion) requireVersion(request.recommendedEngineVersion);
    if (!path.isAbsolute(request.archivePath || '')) throw new Error('アセットのZIPを確認してください');
    requireZIPName(request.fileName);
  }
}

export function updateContentCatalog(catalog, request, packageInfo) {
  const key = request.kind === 'sample' ? 'samples' : 'assets';
  const entries = catalog[key] || [];
  if (!Array.isArray(entries)) throw new Error('配布一覧の形式が不正です');
  if (request.action === 'withdraw') return {...catalog, [key]:entries.filter(entry => entry.id !== request.id)};
  if (request.kind === 'sample' && !catalog.engines.some(engine => engine.version === request.engineVersion))
    throw new Error('対応するエンジンを先に公開してください');
  const sameVersion = entry => entry.id === request.id && entry.version === request.version &&
    (request.kind !== 'sample' || entry.engineVersion === request.engineVersion);
  const previous = entries.find(sameVersion);
  if (previous && previous.package.sha256 !== packageInfo.sha256) throw new Error('公開済みの内容を更新する場合は新しい版番号を指定してください');
  const entry = {id:request.id, name:request.name.trim(), description:request.description || '', version:request.version,
    ...(request.kind === 'sample' ? {engineVersion:request.engineVersion} :
      {recommendedEngineVersion:request.recommendedEngineVersion || '', fileName:request.fileName}), package:packageInfo};
  return {...catalog, [key]:[...entries.filter(item => !sameVersion(item)), entry]};
}

export async function resolveBaseURL(value) {
  if (value) return requireBaseURL(value);
  for (const file of [path.join(repository, 'Generated/Distribution/channel.json'),
    path.join(process.env.LOCALAPPDATA || '', 'NEMEngine/Launcher/state.json')]) {
    try {
      const data = JSON.parse(await readFile(file, 'utf8'));
      const candidate = data.baseUrl || data.catalogUrl?.replace(/\/catalog\.json$/, '');
      if (candidate) return requireBaseURL(candidate);
    } catch { }
  }
  throw new Error('配布先URLを設定してください ランチャーの配布設定またはGenerated/Distribution/channel.jsonを確認してください');
}

export async function publishContent(request, suppliedClient, signal) {
  validateRequest(request);
  const baseURL = await resolveBaseURL(request.baseUrl);
  const client = suppliedClient || await createPublicationClient({baseURL, signal});
  const current = await readCatalog(client);
  if (request.action === 'withdraw') {
    const next = updateContentCatalog(current.catalog, request);
    signal?.throwIfAborted();
    await writeCatalog(client, next, current.etag);
    if (!suppliedClient) await cacheCatalog(next);
    console.log('配布一覧から取り下げました: ' + request.id);
    return next;
  }
  const root = path.join(repository, 'Generated/DistributionContent');
  await mkdir(root, {recursive:true});
  const stage = await mkdtemp(path.join(root, 'publish-'));
  try {
    let archive = request.archivePath;
    const infoFile = path.join(stage, 'archive.json');
    const argumentsList = ['-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
      fileURLToPath(new URL('ContentArchive.ps1', import.meta.url)), '-InfoFile', infoFile];
    if (request.kind === 'sample') {
      const manifest = JSON.parse(await readFile(path.join(request.sourceDirectory, 'template.json'), 'utf8'));
      if (manifest.id !== request.id || manifest.kind !== 'Sample' || manifest.schemaVersion !== 1)
        throw new Error('サンプルの保存元と配布対象が一致しません');
      archive = path.join(stage, 'project.zip');
      argumentsList.push('-SourceDirectory', request.sourceDirectory, '-OutputArchive', archive);
    } else argumentsList.push('-ArchivePath', archive);
    const {stdout} = await execute('powershell.exe', argumentsList, {cwd:repository, windowsHide:true, maxBuffer:1024 * 1024, signal});
    if (stdout.trim()) console.log(stdout.trim());
    const info = JSON.parse(await readFile(infoFile, 'utf8'));
    const sha256 = await hashFile(archive, signal);
    const key = request.kind === 'sample' ? `samples/${request.id}/${request.version}/${request.engineVersion}/${sha256}/project.zip` :
      `assets/${request.id}/${request.version}/${sha256}/asset.zip`;
    const id = `${request.kind}-${request.id}-${request.version}` + (request.kind === 'sample' ? '-' + request.engineVersion : '');
    requireID(id);
    const packageInfo = {id, url:baseURL + '/' + key, sha256, bytes:info.bytes, unpackedBytes:info.unpackedBytes};
    const next = updateContentCatalog(current.catalog, request, packageInfo);
    validateContentCatalog(next);
    signal?.throwIfAborted();
    await uploadArchive(client, archive, key, packageInfo, {verifiedHash:sha256, signal});
    signal?.throwIfAborted();
    await writeCatalog(client, next, current.etag);
    if (!suppliedClient) await cacheCatalog(next);
    await writeFile(path.join(stage, 'published.json'), JSON.stringify(packageInfo));
    console.log('公開しました: ' + request.name);
    return next;
  } finally { await rm(stage, {recursive:true, force:true, maxRetries:3, retryDelay:200}); }
}

async function cacheCatalog(catalog) {
  const directory = path.join(repository, 'Generated/Distribution');
  await mkdir(directory, {recursive:true});
  const temporary = path.join(directory, `catalog.${process.pid}.writing`);
  try {
    await writeFile(temporary, JSON.stringify(catalog, null, 2) + '\n');
    await rename(temporary, path.join(directory, 'catalog.json'));
  } finally { await rm(temporary, {force:true}); }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const controller = new AbortController();
  let timer;
  try {
    if (process.argv.length !== 4 || process.argv[2] !== '--request') throw new Error('使用方法: node content.mjs --request request.json');
    timer = setInterval(() => {
      void access(process.argv[3] + '.cancel').then(() => controller.abort(), () => {});
    }, 200);
    await publishContent(JSON.parse(await readFile(process.argv[3], 'utf8')), undefined, controller.signal);
  } catch (error) {
    // 子プロセスの引数や認証出力をログへ含めない
    console.error(controller.signal.aborted ? '操作をキャンセルしました' : error.stderr?.trim() || error.message);
    process.exitCode = controller.signal.aborted ? 2 : 1;
  } finally { clearInterval(timer); }
}
