import {readFile, stat} from 'node:fs/promises';
import {fileURLToPath} from 'node:url';
import path from 'node:path';
import {resolveBaseURL} from './content.mjs';
import {createPublicationClient, hashFile, readCatalog, uploadArchive, writeCatalog} from './publication.mjs';
import {requireText, requireVersion, validateContentCatalog, validatePackage} from './distribution-validation.mjs';

function engineKey(version, sha256) {
  requireVersion(version);
  if (typeof sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(sha256)) throw new Error('エンジンのSHA-256が不正です');
  return `releases/${version}/${sha256}/engine.zip`;
}

function summary(catalog, etag) {
  return {etag, engines:catalog.engines, retiredVersions:catalog.retiredEngineVersions || [],
    pendingDeletes:(catalog.pendingEngineDeletes || []).map(item => item.version), samples:catalog.samples || [],
    assets:catalog.assets || []};
}

async function commit(client, catalog, etag) {
  validateContentCatalog(catalog);
  try {
    return await writeCatalog(client, catalog, etag);
  } catch (error) {
    if ([409, 412].includes(error.$metadata?.httpStatusCode)) throw new Error('配布一覧が他の操作で更新されました 一覧を再取得してください');
    throw error;
  }
}

async function uploadRelease(request, client, catalog, baseURL, legacy) {
  const release = JSON.parse(await readFile(request.releasePath, 'utf8'));
  if (release.version !== request.version) throw new Error('指定した版と配布ファイルの版が一致しません');
  requireVersion(release.minimumLauncherVersion);
  requireVersion(release.toolchainVersion);
  const key = engineKey(request.version, release.engine.sha256);
  const toolchainKey = 'toolchains/' + release.toolchain.id + '.zip';
  release.engine.url = baseURL + '/' + key;
  release.toolchain.url = baseURL + '/' + toolchainKey;
  validatePackage(release.engine);
  validatePackage(release.toolchain);
  const previous = catalog.engines.find(item => item.version === release.version);
  if (previous && (!legacy || previous.engine.sha256 !== release.engine.sha256 ||
      previous.toolchain.sha256 !== release.toolchain.sha256 || previous.toolchainVersion !== release.toolchainVersion))
    throw new Error('公開済みの版には新しい番号を付けてください');
  const dir = path.dirname(path.resolve(request.releasePath));
  await uploadArchive(client, path.join(dir, release.engine.id + '.zip'), key, release.engine);
  await uploadArchive(client, path.join(dir, release.toolchain.id + '.zip'), toolchainKey, release.toolchain);
  release.notes = request.notes;
  catalog.engines = [...catalog.engines.filter(item => item.version !== release.version), release];
}

async function uploadLauncher(setupPath, client, catalog, baseURL) {
  const props = await readFile(new URL('./LauncherVersion.props', import.meta.url), 'utf8');
  const version = props.match(/<Version>(\d+\.\d+\.\d+)<\/Version>/)?.[1];
  requireVersion(version);
  const sha256 = await hashFile(setupPath);
  if (catalog.launcher?.version === version && catalog.launcher.setup.sha256 !== sha256)
    throw new Error('ランチャーも更新する場合はLauncherVersion.propsの版を上げてください');
  const key = `launcher/${version}/${sha256}/NEMLauncherSetup.exe`;
  const bytes = (await stat(setupPath)).size;
  const setup = {id:`nemlauncher-${version}-win-x64`, url:baseURL + '/' + key, bytes, unpackedBytes:bytes, sha256};
  await uploadArchive(client, setupPath, key, setup);
  catalog.launcher = {version, setup};
}

async function removePendingZIP(client, catalog, version, etag, baseURL) {
  const pending = catalog.pendingEngineDeletes?.find(item => item.version === version);
  if (!pending) throw new Error('削除待ちのZIPがありません');
  const key = engineKey(version, pending.sha256);
  if (catalog.engines.some(engine => engine.engine.url === baseURL + '/' + key))
    throw new Error('ほかのエンジンがこのZIPを参照しています');
  try {
    await client.remove(key);
    if (await client.head(key)) throw new Error('配布元にZIPが残っています');
    catalog.pendingEngineDeletes = catalog.pendingEngineDeletes.filter(item => item.version !== version);
    return summary(catalog, await commit(client, catalog, etag));
  } catch (error) {
    throw new Error('版は配布一覧から削除されました。一覧を更新してZIPの削除を再試行してください。' + error.message);
  }
}

export async function manageRelease(request, suppliedClient, legacy) {
  const baseURL = await resolveBaseURL(request.baseURL);
  const client = suppliedClient || await createPublicationClient({baseURL});
  let {catalog, etag} = await readCatalog(client);
  validateContentCatalog(catalog);
  if (request.action === 'list') return summary(catalog, etag);
  requireVersion(request.version);
  if (!legacy && request.etag !== etag) throw new Error('配布一覧が更新されました 一覧を再取得してください');
  if (request.action === 'cleanup') return removePendingZIP(client, catalog, request.version, etag, baseURL);
  const version = request.version;
  const release = catalog.engines.find(item => item.version === version);
  switch (request.action) {
    case 'publish':
      if (catalog.retiredEngineVersions?.includes(version)) throw new Error('削除した版番号は再利用できません');
      requireText(request.notes, 4000);
      await uploadRelease(request, client, catalog, baseURL, legacy);
      if (legacy?.setupPath) await uploadLauncher(legacy.setupPath, client, catalog, baseURL);
      break;
    case 'notes':
      if (!release) throw new Error('指定した版は公開されていません');
      requireText(request.notes, 4000);
      release.notes = request.notes;
      break;
    case 'delete': {
      if (!release) throw new Error('指定した版は公開されていません');
      const samples = (catalog.samples || []).filter(item => item.engineVersion === version);
      if (samples.length) throw new Error('この版を使用する公開サンプルがあります: ' +
        samples.map(item => item.name + ' (' + item.version + ')').join('、'));
      const key = engineKey(version, release.engine.sha256);
      if (release.engine.url !== baseURL + '/' + key || catalog.engines.some(item => item !== release && item.engine.url === release.engine.url))
        throw new Error('この版専用の配布ZIPを特定できません');
      catalog.engines = catalog.engines.filter(item => item !== release);
      catalog.retiredEngineVersions = [...catalog.retiredEngineVersions || [], version];
      catalog.pendingEngineDeletes = [...catalog.pendingEngineDeletes || [], {version, sha256:release.engine.sha256}];
      // 一覧から取り下げた後にZIPを消す。中断時は削除待ちから再開できる
      etag = await commit(client, catalog, etag);
      return removePendingZIP(client, catalog, version, etag, baseURL);
    }
    default: throw new Error('不明なエンジン配布操作です');
  }
  return summary(catalog, await commit(client, catalog, etag));
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  try {
    if (process.argv.length !== 3) throw new Error('使用方法: node engine-release.mjs request.json');
    const request = JSON.parse(await readFile(process.argv[2], 'utf8'));
    console.log(JSON.stringify(await manageRelease(request)));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
