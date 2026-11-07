import {readFile, stat} from 'node:fs/promises';
import path from 'node:path';
import {createPublicationClient, hashFile, readCatalog, writeCatalog, uploadArchive, requireBaseURL, requireVersion} from './publication.mjs';

const args = process.argv.slice(2);
if (args.length < 2 || args.length > 3) throw new Error('使用方法: node publish.mjs release-0.1.0.json HTTPS配布URL [NEMLauncherSetup.exe]');
const [releasePath, baseURLValue, setupPath] = args;
const baseURL = requireBaseURL(baseURLValue);
const launcherVersion = (await readFile(new URL('./LauncherVersion.props', import.meta.url), 'utf8')).match(/<Version>(\d+\.\d+\.\d+)<\/Version>/)?.[1];
requireVersion(launcherVersion);
const release = JSON.parse(await readFile(releasePath, 'utf8'));
requireVersion(release.version);
const directory = path.dirname(path.resolve(releasePath));
const client = await createPublicationClient({baseURL});
const {catalog, etag} = await readCatalog(client);
const previous = catalog.engines.find(item => item.version === release.version);
if (previous && (previous.engine.sha256 !== release.engine.sha256 || previous.toolchain.sha256 !== release.toolchain.sha256 ||
    previous.toolchainVersion !== release.toolchainVersion)) throw new Error('公開済みの版には新しい番号を付けてください');
const engineKey = `releases/${release.version}/${release.engine.sha256}/engine.zip`;
const toolchainKey = `toolchains/${release.toolchain.id}.zip`;
await uploadArchive(client, path.join(directory, release.engine.id + '.zip'), engineKey, release.engine);
await uploadArchive(client, path.join(directory, release.toolchain.id + '.zip'), toolchainKey, release.toolchain);
release.engine.url = baseURL + '/' + engineKey;
release.toolchain.url = baseURL + '/' + toolchainKey;
catalog.engines = [...catalog.engines.filter(item => item.version !== release.version), release];
if (setupPath) {
  const sha256 = await hashFile(setupPath);
  if (catalog.launcher?.version === launcherVersion && catalog.launcher.setup.sha256 !== sha256)
    throw new Error('ランチャーも更新する場合はLauncherVersion.propsの版を上げてください');
  const key = `launcher/${launcherVersion}/${sha256}/NEMLauncherSetup.exe`;
  await uploadArchive(client, setupPath, key);
  const bytes = (await stat(setupPath)).size;
  catalog.launcher = {version:launcherVersion, setup:{id:`nemlauncher-${launcherVersion}-win-x64`,
    url:baseURL + '/' + key, bytes, unpackedBytes:bytes, sha256}};
}
// 全配布物の転送後に一覧だけを切り替える
await writeCatalog(client, catalog, etag);
console.log('公開しました: ' + baseURL + '/catalog.json');
