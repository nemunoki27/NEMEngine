const artifact = /^(releases\/\d+\.\d+\.\d+\/[a-f0-9]{64}\/engine\.zip|toolchains\/[a-zA-Z0-9.-]+\.zip|launcher\/\d+\.\d+\.\d+\/[a-f0-9]{64}\/NEMLauncherSetup\.exe|samples\/[a-zA-Z0-9.-]+\/\d+\.\d+\.\d+\/\d+\.\d+\.\d+\/[a-f0-9]{64}\/project\.zip|assets\/[a-zA-Z0-9.-]+\/\d+\.\d+\.\d+\/[a-f0-9]{64}\/[a-zA-Z0-9][a-zA-Z0-9._-]*\.zip)$/;

export const isArtifactKey = key => artifact.test(key);

// Cloudflareで認証済みの開発者だけに公開操作を許可する
async function authorize(request, env) {
  const authorization = request.headers.get('authorization');
  if (!env.PUBLISHER_USER_ID || !authorization?.startsWith('Bearer ')) return false;
  const response = await fetch('https://api.cloudflare.com/client/v4/user', {headers:{Authorization:authorization}});
  if (!response.ok) return false;
  const result = await response.json();
  return result.success && result.result?.id === env.PUBLISHER_USER_ID;
}

export async function managePublication(request, env, operation) {
  if (!await authorize(request, env)) return new Response('Not found', {status:404});
  if (operation === 'catalog') {
    if (request.method === 'GET') {
      const object = await env.DISTRIBUTION.get('catalog.json');
      return Response.json(object ? {catalog:await object.json(), etag:object.httpEtag} : {catalog:null, etag:null},
        {headers:{'Cache-Control':'no-store'}});
    }
    if (request.method !== 'PUT') return new Response('Method not allowed', {status:405});
    if (!request.headers.has('if-match') && request.headers.get('if-none-match') !== '*')
      return new Response('Precondition required', {status:428});
    const text = await request.text();
    if (text.length > 2 * 1024 * 1024) return new Response('Catalog too large', {status:413});
    try { validateContentCatalog(JSON.parse(text)); } catch { return new Response('Invalid catalog', {status:400}); }
    const stored = await env.DISTRIBUTION.put('catalog.json', text, {onlyIf:request.headers,
      httpMetadata:{contentType:'application/json', cacheControl:'no-cache'}});
    return stored ? Response.json({etag:stored.httpEtag}) : new Response('Catalog changed', {status:412});
  }
  if (operation !== 'upload') return new Response('Not found', {status:404});
  const url = new URL(request.url);
  const key = url.searchParams.get('key') || '';
  const action = url.searchParams.get('action');
  if (!isArtifactKey(key)) return new Response('Invalid artifact', {status:400});
  if (action === 'create' && request.method === 'POST') {
    const sha256 = request.headers.get('x-content-sha256') || '';
    if (!/^[a-f0-9]{64}$/.test(sha256)) return new Response('Invalid hash', {status:400});
    const upload = await env.DISTRIBUTION.createMultipartUpload(key, {
      httpMetadata:{contentType:key.endsWith('.zip') ? 'application/zip' : 'application/octet-stream'}, customMetadata:{sha256}});
    return Response.json({uploadID:upload.uploadId});
  }
  const uploadID = url.searchParams.get('uploadID');
  if (!uploadID) return new Response('Invalid upload', {status:400});
  const upload = env.DISTRIBUTION.resumeMultipartUpload(key, uploadID);
  if (action === 'part' && request.method === 'PUT') {
    const part = Number(url.searchParams.get('part'));
    if (!Number.isInteger(part) || part < 1 || part > 10000 || !request.body)
      return new Response('Invalid part', {status:400});
    return Response.json(await upload.uploadPart(part, request.body));
  }
  if (action === 'complete' && request.method === 'POST') {
    const parts = await request.json();
    if (!Array.isArray(parts) || parts.length < 1 || parts.length > 10000) return new Response('Invalid parts', {status:400});
    const result = await upload.complete(parts);
    return Response.json({etag:result.httpEtag, bytes:result.size});
  }
  if (action === 'abort' && request.method === 'DELETE') {
    await upload.abort();
    return new Response(null, {status:204});
  }
  return new Response('Invalid operation', {status:400});
}
import {validateContentCatalog} from './distribution-validation.mjs';

