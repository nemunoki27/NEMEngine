export function requireVersion(value) {
  if (typeof value !== 'string' || !/^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(value) ||
      value.split('.').some(part => Number(part) > 2147483647)) throw new Error('版番号は0.1.0の形式で指定してください');
}

export function requireID(value) {
  if (typeof value !== 'string' || !/^[a-zA-Z0-9][a-zA-Z0-9.-]{0,127}$/.test(value)) throw new Error('配布物の識別子が不正です');
}

export function requireBaseURL(value) {
  if (typeof value !== 'string') throw new Error('配布先のHTTPS URLが不正です');
  const url = new URL(value);
  if (url.protocol !== 'https:' || url.search || url.hash || url.username || url.password) throw new Error('配布先のHTTPS URLが不正です');
  return url.href.replace(/\/$/, '');
}

export function requireText(value, maximum, required = false) {
  if (typeof value !== 'string' || value.length > maximum || (required && !value.trim())) throw new Error('配布名または説明を確認してください');
}

export function requireZIPName(value) {
  requireText(value, 150, true);
  if (/[<>:"/\\|?*\x00-\x1f]/.test(value) || !value.toLowerCase().endsWith('.zip') ||
      /^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)/i.test(value)) throw new Error('配布ファイル名を確認してください');
}

function validatePackage(value) {
  if (!value || typeof value !== 'object') throw new Error('配布物の検証情報が不正です');
  requireID(value.id);
  const url = new URL(value.url);
  if (url.protocol !== 'https:' || typeof value.sha256 !== 'string' || !/^[a-f0-9]{64}$/.test(value.sha256) ||
      !Number.isSafeInteger(value.bytes) || value.bytes <= 0 || !Number.isSafeInteger(value.unpackedBytes) || value.unpackedBytes <= 0)
    throw new Error('配布物の検証情報が不正です');
}

// ランチャーが読み込める内容だけを配布一覧へ保存する
export function validateContentCatalog(catalog) {
  if (!catalog || catalog.schemaVersion !== 1 || !Array.isArray(catalog.engines)) throw new Error('配布一覧の形式が不正です');
  for (const kind of ['samples', 'assets']) {
    if (catalog[kind] !== undefined && !Array.isArray(catalog[kind])) throw new Error('配布一覧の形式が不正です');
    const versions = new Set();
    for (const entry of catalog[kind] || []) {
      requireID(entry.id);
      requireVersion(entry.version);
      requireText(entry.name, 120, true);
      requireText(entry.description ?? '', 4000);
      if (entry.description === null) throw new Error('配布内容の説明が不正です');
      let identity = entry.id + '/' + entry.version;
      if (kind === 'samples') {
        requireVersion(entry.engineVersion);
        if (!catalog.engines.some(engine => engine.version === entry.engineVersion)) throw new Error('サンプルの対応エンジンがありません');
        identity += '/' + entry.engineVersion;
      } else {
        if (entry.recommendedEngineVersion !== undefined && entry.recommendedEngineVersion !== '') requireVersion(entry.recommendedEngineVersion);
        requireZIPName(entry.fileName);
      }
      if (versions.has(identity)) throw new Error('同じ版の配布情報が重複しています');
      versions.add(identity);
      validatePackage(entry.package);
    }
  }
}
