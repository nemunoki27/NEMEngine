import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

export const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const website = path.join(root, 'Docs/Website');
export const output = path.join(root, 'Generated/Documentation');
export const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replace(/\r\n/g, '\n');
export const json = relative => JSON.parse(read(relative));
// API識別子とコードを保ち、説明文の呼び方を揃える
export const formatDocumentation = text => text.split(/(```[\s\S]*?```|`[^`\n]+`)/g)
	.map((part, index) => index % 2 ? part : part.replace(/(?<![A-Za-z0-9_])Entity(?![A-Za-z0-9_])/g, 'GameObject')).join('');
export const write = (target, text) => {

	fs.mkdirSync(path.dirname(target), { recursive: true });
	fs.writeFileSync(target, text.replace(/\r\n/g, '\n').replace(/\n/g, '\r\n'));
};
export const walk = directory => fs.readdirSync(directory, { withFileTypes: true }).flatMap(entry => {

	if (['node_modules', 'bin', 'obj', '.git', '.vitepress'].includes(entry.name)) return [];
	const full = path.join(directory, entry.name);
	return entry.isDirectory() ? walk(full) : [full];
});
