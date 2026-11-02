import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {readFileSync} from 'node:fs';
import worker,{cleanup} from './worker.mjs';

function environment() {
  const db = new DatabaseSync(':memory:');
  db.exec(readFileSync(new URL('./schema.sql',import.meta.url),'utf8'));
  const objects = new Map();
  return {IP_SALT:'test-only-salt', objects, db,
    REPORTS:{prepare(sql) { return {bind(...args) { return {
      async first() { return db.prepare(sql).get(...args) ?? null; },
      async all() { return {results:db.prepare(sql).all(...args)}; },
      async run() { return {meta:{changes:Number(db.prepare(sql).run(...args).changes)}}; }
    }; }}; }},
    REPORT_FILES:{async put(key,value) { objects.set(key,value); },async delete(keys) { for (const key of keys) objects.delete(key); }}
  };
}
function report(extra={}) {
  return {id:crypto.randomUUID(),title:'人工テスト',description:'再現手順の人工データ',contact:'',kind:'Engine',projectName:'Fixture',
    launcherVersion:'0.1.8',engineVersion:'0.1.3',windowsVersion:'test',attachLogs:false,logs:[],...extra};
}
function request(value,ip='192.0.2.1') { return new Request('https://unit.invalid/reports', {method:'POST',headers:{'Content-Type':'application/json','cf-connecting-ip':ip},body:JSON.stringify(value)}); }

test('匿名受付と非公開の保存を確認する', async () => {
  const env=environment(), value=report();
  const response=await worker.fetch(request(value),env);
  assert.equal(response.status,201);
  const result=await response.json();
  assert.equal(result.id,value.id);
  assert.equal(env.objects.size,1);
  assert.equal(env.db.prepare('SELECT state FROM reports').get().state,'ready');
  assert.equal((await worker.fetch(new Request('https://unit.invalid/reports'),env)).status,405);
  assert.equal((await worker.fetch(new Request('https://unit.invalid/'+value.id+'/report.json'),env)).status,404);
  assert.equal(response.headers.get('cache-control'),'no-store');
  assert.equal(env.db.prepare('SELECT ip_hash FROM reports').get().ip_hash.includes('192.0.2'),false);
});
test('添付ONの指定ログだけ保存する', async () => {
  const env=environment(), value=report({attachLogs:true,logs:[{kind:'Launcher',text:'fixture launcher',truncated:false},{kind:'Engine',text:'fixture engine',truncated:true}]});
  assert.equal((await worker.fetch(request(value),env)).status,201);
  const metadata=JSON.parse([...env.objects.entries()].find(([key])=>key.endsWith('report.json'))[1]);
  assert.equal(metadata.logs.length,2);
  assert.equal('text' in metadata.logs[0],false);
  assert.deepEqual([...env.objects.values()].filter(value=>!value.startsWith('{')).sort(),['fixture engine','fixture launcher']);
});
test('再送は重複作成せず、内容の変更は拒否する', async () => {
  const env=environment(), value=report();
  const first=await (await worker.fetch(request(value),env)).json();
  const second=await worker.fetch(request(value),env);
  assert.equal(second.status,200);
  assert.deepEqual(await second.json(),first);
  assert.equal(env.db.prepare('SELECT COUNT(*) AS n FROM reports').get().n,1);
  assert.equal((await worker.fetch(request({...value,title:'changed'}),env)).status,409);
});
test('入力と添付の上限を確認する', async () => {
  for (const extra of [{title:''},{description:''},{title:'x'.repeat(201)},{contact:'x'.repeat(257)},
    {id:'../escape'},{attachLogs:false,logs:[{kind:'Launcher',text:'x',truncated:false}]},
    {attachLogs:true,logs:[{kind:'../secret',text:'x',truncated:false}]},
    {attachLogs:true,logs:[{kind:'Engine',text:'x'.repeat(1048577),truncated:false}]},
    {attachLogs:true,logs:[{kind:'Engine',text:'a',truncated:false},{kind:'Engine',text:'b',truncated:false}]}]) {
    const env=environment();
    assert.equal((await worker.fetch(request(report(extra)),env)).status,400);
    assert.equal(env.objects.size,0);
  }
  const tooLarge=request(report());
  tooLarge.headers.set('content-length',String(4*1048576+1));
  assert.equal((await worker.fetch(tooLarge,environment())).status,413);
  const stream=new ReadableStream({start(controller) { controller.enqueue(new Uint8Array(4*1048576+1));controller.close(); }});
  const largeStream=new Request('https://unit.invalid/reports',{method:'POST',headers:{'Content-Type':'application/json'},body:stream,duplex:'half'});
  assert.equal((await worker.fetch(largeStream,environment())).status,413);
});
test('IPごとの毎時10件と全体の毎日50件を制限する', async () => {
  const env=environment();
  for(let i=0;i<10;i++) assert.equal((await worker.fetch(request(report()),env)).status,201);
  assert.equal((await worker.fetch(request(report()),env)).status,429);
  for(let i=0;i<40;i++) assert.equal((await worker.fetch(request(report(),'192.0.2.'+(i+2)),env)).status,201);
  assert.equal((await worker.fetch(request(report(),'192.0.2.250'),env)).status,429);
});
test('添付失敗から再送し、未完成を公開しない', async () => {
  const env=environment(), value=report({attachLogs:true,logs:[{kind:'Engine',text:'fixture',truncated:false}]});
  const put=env.REPORT_FILES.put;
  env.REPORT_FILES.put=async()=>{throw new Error('fixture failure');};
  assert.equal((await worker.fetch(request(value),env)).status,503);
  assert.equal(env.db.prepare('SELECT state FROM reports').get().state,'pending');
  env.REPORT_FILES.put=put;
  assert.equal((await worker.fetch(request(value),env)).status,201);
  assert.equal(env.db.prepare('SELECT COUNT(*) AS n FROM reports').get().n,1);
});
test('30日経過した報告と1日経過した未完成添付を削除する', async () => {
  const env=environment(), expired=report(), pending=report(), current=report();
  for(const value of [expired,pending,current]) assert.equal((await worker.fetch(request(value),env)).status,201);
  const now=Math.floor(Date.now()/1000);
  env.db.prepare('UPDATE reports SET received_at=? WHERE id=?').run(now-31*86400,expired.id);
  env.db.prepare("UPDATE reports SET received_at=?,state='pending' WHERE id=?").run(now-2*86400,pending.id);
  await cleanup(env,now);
  assert.deepEqual(env.db.prepare('SELECT id FROM reports').all().map(row=>row.id),[current.id]);
  assert.equal(env.objects.size,1);
});
