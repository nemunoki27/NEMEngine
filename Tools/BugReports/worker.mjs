const MAX_REQUEST = 4 * 1048576;
const MAX_LOG = 1048576;
const fileNames = {Launcher:'launcher.log', Engine:'engine.log', Game:'gameLogic.log'};
const uuid = /^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$/;
const headers = {'Content-Type':'application/json; charset=utf-8', 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff', 'X-Robots-Tag':'noindex, nofollow, noarchive'};
const json = (value, status=200) => new Response(JSON.stringify(value), {status, headers});
const bytes = value => new TextEncoder().encode(value).byteLength;
const hash = async value => Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(value))), byte => byte.toString(16).padStart(2,'0')).join('');

export function validate(value) {
  if (!value || typeof value !== 'object' || !uuid.test(value.id) || !['Launcher','Engine'].includes(value.kind)) throw new Error('input');
  const fields = {title:200, description:16000, contact:256, projectName:256, launcherVersion:64, engineVersion:64, windowsVersion:256};
  const clean = {id:value.id, kind:value.kind};
  for (const [key, limit] of Object.entries(fields)) {
    if (typeof value[key] !== 'string' || value[key].length > limit) throw new Error('input');
    clean[key] = value[key];
  }
  if (!clean.title.trim() || !clean.description.trim() || typeof value.attachLogs !== 'boolean' || !Array.isArray(value.logs) || value.logs.length > 3) throw new Error('input');
  if (!value.attachLogs && value.logs.length) throw new Error('input');
  const kinds = new Set();
  clean.attachLogs = value.attachLogs;
  clean.logs = value.logs.map(log => {
    if (!log || !Object.hasOwn(fileNames, log.kind) || typeof log.text !== 'string' || bytes(log.text) > MAX_LOG || typeof log.truncated !== 'boolean' || kinds.has(log.kind)) throw new Error('input');
    kinds.add(log.kind);
    return {kind:log.kind, text:log.text, truncated:log.truncated};
  });
  return clean;
}

async function readBody(request) {
  if (Number(request.headers.get('content-length')) > MAX_REQUEST) throw new RangeError('size');
  if (!request.body) throw new Error('input');
  const reader = request.body.getReader();
  const blocks = [];
  let size = 0;
  try {
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      size += value.byteLength;
      if (size > MAX_REQUEST) { await reader.cancel(); throw new RangeError('size'); }
      blocks.push(value);
    }
  } finally { reader.releaseLock(); }
  const data = new Uint8Array(size);
  let offset = 0;
  for (const block of blocks) { data.set(block, offset); offset += block.byteLength; }
  return validate(JSON.parse(new TextDecoder('utf-8', {fatal:true}).decode(data)));
}

function receipt(row) { return {id:row.id, receivedAt:new Date(row.received_at * 1000).toISOString()}; }

export async function submit(request, env, now=Math.floor(Date.now()/1000)) {
  if (!env.IP_SALT || !env.REPORTS || !env.REPORT_FILES) return json({error:'unavailable'},503);
  let report;
  try { report = await readBody(request); }
  catch (error) { return json({error:error instanceof RangeError ? 'size' : 'input'}, error instanceof RangeError ? 413 : 400); }
  const payloadHash = await hash(JSON.stringify(report));
  const ipHash = await hash(env.IP_SALT + ':' + (request.headers.get('cf-connecting-ip') || 'unknown'));
  let row = await env.REPORTS.prepare('SELECT * FROM reports WHERE id = ?').bind(report.id).first();
  if (row && row.payload_hash !== payloadHash) return json({error:'conflict'},409);
  if (row?.state === 'ready') return json(receipt(row));
  if (!row) {
    const date = new Date(now * 1000).toISOString().slice(0,10);
    const prefix = `${date}/${report.id}/`;
    const keys = [prefix+'report.json', ...report.logs.map(log => prefix+fileNames[log.kind])];
    // 受付数の判定と予約を同じSQL文で確定する
    const inserted = await env.REPORTS.prepare(`INSERT OR IGNORE INTO reports
      (id,received_at,ip_hash,payload_hash,state,title,kind,launcher_version,engine_version,object_keys)
      SELECT ?,?,?,?,'pending',?,?,?,?,? WHERE
      (SELECT COUNT(*) FROM reports WHERE ip_hash=? AND received_at>?) < 10 AND
      (SELECT COUNT(*) FROM reports WHERE received_at>=?) < 50`)
      .bind(report.id,now,ipHash,payloadHash,report.title,report.kind,report.launcherVersion,report.engineVersion,JSON.stringify(keys),ipHash,now-3600,Math.floor(now/86400)*86400).run();
    row = await env.REPORTS.prepare('SELECT * FROM reports WHERE id = ?').bind(report.id).first();
    if (!row) return json({error:'rate'},429);
    if (row.payload_hash !== payloadHash) return json({error:'conflict'},409);
    if (row.state === 'ready') return json(receipt(row));
    if (!inserted.meta.changes) return json({error:'retry'},503);
  } else {
    const attempt = await env.REPORTS.prepare("UPDATE reports SET attempts=attempts+1 WHERE id=? AND state='pending' AND attempts<5").bind(report.id).run();
    if (!attempt.meta.changes) {
      const current = await env.REPORTS.prepare('SELECT * FROM reports WHERE id = ?').bind(report.id).first();
      return current?.state === 'ready' ? json(receipt(current)) : json({error:'rate'},429);
    }
  }
  const keys = JSON.parse(row.object_keys);
  const metadata = {...report, logs:report.logs.map(({kind,truncated}) => ({kind,truncated})), receivedAt:receipt(row).receivedAt};
  await Promise.all([env.REPORT_FILES.put(keys[0],JSON.stringify(metadata),{httpMetadata:{contentType:'application/json; charset=utf-8'}}),
    ...report.logs.map((log,index) => env.REPORT_FILES.put(keys[index+1],log.text,{httpMetadata:{contentType:'text/plain; charset=utf-8'}}))]);
  await env.REPORTS.prepare("UPDATE reports SET state='ready' WHERE id=?").bind(report.id).run();
  return json(receipt(row),201);
}

export async function cleanup(env, now=Math.floor(Date.now()/1000)) {
  // 古い受付と中断された添付を同じ記録から回収する
  for (;;) {
    const result = await env.REPORTS.prepare("SELECT id, object_keys FROM reports WHERE received_at<? OR (state='pending' AND received_at<?) LIMIT 100")
      .bind(now-30*86400, now-86400).all();
    if (!result.results.length) break;
    for (const row of result.results) {
      await env.REPORT_FILES.delete(JSON.parse(row.object_keys));
      await env.REPORTS.prepare('DELETE FROM reports WHERE id=?').bind(row.id).run();
    }
  }
}

export default {
  async fetch(request, env) {
    if (new URL(request.url).pathname !== '/reports') return new Response('Not found',{status:404});
    if (request.method !== 'POST') return new Response('Method not allowed',{status:405,headers:{Allow:'POST'}});
    if (!request.headers.get('content-type')?.toLowerCase().startsWith('application/json')) return json({error:'type'},415);
    try { return await submit(request,env); }
    catch { return json({error:'unavailable'},503); }
  },
  async scheduled(_event,env) { await cleanup(env); }
};
