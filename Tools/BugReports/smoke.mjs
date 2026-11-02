import {mkdir, writeFile} from 'node:fs/promises';
import path from 'node:path';

const endpoint = 'https://nemengine-support.nemengine-distribution.workers.dev/reports';
const output = path.resolve(import.meta.dirname,'../../Generated/BugReports/smoke.json');
const receipts = [];
for (const attachLogs of [false,true]) {
  // 実際のログや個人情報は検証へ使用しない
  const report = {id:crypto.randomUUID(),title:'配布検証用の人工報告',description:'人工データの送信と非公開取得を確認',contact:'',kind:'Launcher',
    projectName:'Fixture',launcherVersion:'0.1.8',engineVersion:'',windowsVersion:'fixture',attachLogs,
    logs:attachLogs ? [{kind:'Launcher',text:'fixture launcher\n',truncated:false},{kind:'Engine',text:'fixture engine\n',truncated:false}] : []};
  let receipt;
  for (let attempt=0;attempt<2;attempt++) {
    const response = await fetch(endpoint,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(report),redirect:'error'});
    if (!response.ok) throw new Error(`受付失敗: ${response.status}`);
    const received = await response.json();
    if (received.id!==report.id || !Number.isFinite(Date.parse(received.receivedAt))) throw new Error('受付番号が一致しません');
    if (receipt && JSON.stringify(receipt)!==JSON.stringify(received)) throw new Error('再送で受付結果が変わりました');
    receipt = received;
  }
  receipts.push({...receipt,attachLogs});
}
if ((await fetch(endpoint)).status!==405 || (await fetch(new URL('/reports/'+receipts[0].id,endpoint))).status!==404)
  throw new Error('報告の読み取り口が公開されています');
await mkdir(path.dirname(output),{recursive:true});
await writeFile(output,JSON.stringify(receipts,null,2)+'\n');
console.log('人工報告の受付・再送・公開取得の拒否を確認: '+output);
