import path from 'node:path';
import {inventory, inspectorPresentation, widgets} from './inventory.mjs';
import {output, read, write} from './paths.mjs';

const data = inventory();
const presentation = inspectorPresentation();
const bindings = read('Project/Engine/Editor/UI/Inspectors/Common/InspectorCommonBindings.cpp');
const failures = [];
const rows = [];

// 他の型や関数の項目を混ぜずに編集本体を取り出す
function body(source, signature) {
  const start = source.indexOf(signature);
  if (start < 0) return '';
  const first = source.indexOf('{', start);
  let depth = 1, end = first + 1;
  for (; end < source.length && depth; ++end) {
    if (source[end] === '{') ++depth;
    if (source[end] === '}') --depth;
  }
  return source.slice(first + 1, end - 1);
}

for (const component of data.components) {
  const editor = data.editors.find(editor => editor.name === component.registryName);
  const fields = body(bindings, `BindInspectorCommonFields(${component.nativeType}& value)`);
  const common = [...fields.matchAll(/\.(\w+) = &value\.(\w+(?:\.\w+)*)/g)].map(match => {
    const key = match[1][0].toUpperCase() + match[1].slice(1);
    const label = presentation.fields.get(key);
    if (!label) failures.push(`${component.registryName}: 表示定義がありません: ${key}`);
    return {key, label, member:match[2]};
  });
  if (editor) {
    const source = read(editor.source);
    const draw = body(source, `${editor.className}::DrawFields(`) || body(source, `${editor.className}::Draw(`);
    if (!presentation.categories.has(editor.category)) failures.push(`${editor.name}: 未登録のカテゴリです`);
    for (const field of common) {
      const member = field.member.replaceAll('.', '\\.');
      if (new RegExp(`(?:DrawCheckboxField|DrawLayerMaskField)\\([^;]*draft\\.${member}\\b`).test(draw))
        failures.push(`${editor.name}: 共通項目を個別に描画しています: ${field.member}`);
    }
    for (const label of ['Rendering Layer', '対象Layer', 'None (Drop entity here)', '非スケール時間', '開始時再生'])
      if (source.includes('"' + label + '"')) failures.push(`${editor.name}: 旧表記が残っています: ${label}`);
    const header = body(read('Project/' + component.nativeHeader), `struct ${component.nativeType}`);
    for (const member of ['enabled', 'visible', 'renderingLayerMask']) {
      if (new RegExp(`\\b(?:bool|uint32_t)\\s+${member}\\b`).test(header) &&
          !common.some(field => field.member === member)) failures.push(`${editor.name}: 共通接続がありません: ${member}`);
    }
    rows.push({component:editor.name, inspector:editor.source, category:editor.categoryLabel, common,
      individual:widgets(draw).map(field => ({label:field.label, member:field.value}))});
  } else rows.push({component:component.registryName, inspector:null, status:'編集UIなし', common:[]});
}
if (data.editors.length < 40) failures.push('標準Inspectorの登録を取得できません');
write(path.join(output, 'inspector-audit.json'), JSON.stringify({date:new Date().toISOString(),
  registeredComponents:data.components.length, inspectors:data.editors.length, rows, failures}, null, 2));
if (failures.length) {
  console.error(failures.join('\n'));
  process.exitCode = 1;
} else console.log(`Inspector監査成功: 登録${data.components.length}型 / Inspector${data.editors.length}型 / 共通項目${rows.reduce((count,row)=>count+row.common.length,0)}件`);
