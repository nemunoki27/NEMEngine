import {isArtifactKey, managePublication} from './publication-worker.mjs';

// 共有URLから配布物だけを読み出す
export default {
  async fetch(request, env) {
    if (!env.CHANNEL) return new Response('Not found', {status:404});
    const prefix = '/' + env.CHANNEL + '/';
    const pathname = new URL(request.url).pathname;
    if (!pathname.startsWith(prefix)) return new Response('Not found', {status:404});
    const key = pathname.slice(prefix.length);
    if (key.startsWith('manage/')) return managePublication(request, env, key.slice(7));
    if (request.method !== 'GET' && request.method !== 'HEAD') return new Response('Method not allowed', {status:405, headers:{Allow:'GET, HEAD'}});
    if (key === 'launcher/latest/NEMLauncherSetup.exe') {
      const catalog = await env.DISTRIBUTION.get('catalog.json');
      if (!catalog) return new Response('Not found', {status:404});
      try {
        const release = (await catalog.json()).launcher;
        if (!/^\d+\.\d+\.\d+$/.test(release?.version ?? '') || !/^[a-f0-9]{64}$/.test(release?.setup?.sha256 ?? ''))
          throw new Error('Invalid launcher');
        const target = new URL(release.setup.url);
        const expected = prefix + 'launcher/' + release.version + '/' + release.setup.sha256 + '/NEMLauncherSetup.exe';
        if (target.protocol !== 'https:' || target.origin !== new URL(request.url).origin ||
            target.pathname !== expected || target.search || target.hash) throw new Error('Invalid location');
        return new Response(null, {status:302, headers:{Location:target.href, 'Cache-Control':'no-store',
          'X-Content-Type-Options':'nosniff', 'X-Robots-Tag':'noindex, nofollow, noarchive'}});
      } catch { return new Response('Launcher unavailable', {status:503}); }
    }
    if (key !== 'catalog.json' && !isArtifactKey(key))
      return new Response('Not found', {status:404});
    const object = await env.DISTRIBUTION.get(key, {
      onlyIf: request.headers, range: request.headers
    });
    if (!object) return new Response('Not found', {status:404});
    const headers = new Headers();
    object.writeHttpMetadata(headers);
    headers.set('ETag', object.httpEtag);
    headers.set('Accept-Ranges', 'bytes');
    headers.set('X-Content-Type-Options', 'nosniff');
    headers.set('X-Robots-Tag', 'noindex, nofollow, noarchive');
    headers.set('Cache-Control', key === 'catalog.json' ? 'no-cache' : 'public, max-age=31536000, immutable');
    headers.set('Content-Type', key.endsWith('.json') ? 'application/json' : 'application/octet-stream');
    if (!('body' in object)) return new Response(null, {status:request.headers.has('if-none-match') ? 304 : 412, headers});
    let status = 200;
    if (request.headers.has('range') && object.range && object.range.offset !== undefined) {
      const length = object.range.length ?? object.size - object.range.offset;
      headers.set('Content-Range', `bytes ${object.range.offset}-${object.range.offset + length - 1}/${object.size}`);
      headers.set('Content-Length', String(length));
      status = 206;
    } else headers.set('Content-Length', String(object.size));
    return new Response(request.method === 'HEAD' ? null : object.body, {status, headers});
  }
};
