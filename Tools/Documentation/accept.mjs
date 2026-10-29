import fs from 'node:fs';
import path from 'node:path';
import { root, output, write } from './paths.mjs';

if (!process.argv.includes('--reviewed')) throw new Error('説明とソースを確認した後、--reviewedを指定してください');
const sources = JSON.parse(fs.readFileSync(path.join(output, 'source-hashes.json'), 'utf8'));
write(path.join(root, 'Tools/Documentation/reviewed-sources.json'), JSON.stringify(sources, null, 2));
console.log(`確認済みソース: ${Object.keys(sources).length}件`);
