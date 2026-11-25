const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {normalize, validate, create} = require('./upgrade-editor.js');
const root = '../../code/axmol/CastleGuard/Content/config/';
const data = () => normalize(JSON.parse(fs.readFileSync(new URL(root + 'upgrades.json', `file://${__filename}`))));
const attributes = JSON.parse(fs.readFileSync(new URL(root + 'upgrade-attributes.json', `file://${__filename}`))).attributes;

test('existing catalog migrates to explicit effects without changing values', () => {
  const draft = data();
  assert.equal(validate(draft, attributes), '');
  assert.deepEqual(draft.upgrades[0].effects[0], {attribute: 'arrow_damage', operation: 'set', values: [1,2,3,4,5,6]});
  assert.equal('values' in draft.upgrades[0], false);
});
test('new child uses a free position, unique ID, and neutral initial effect', () => {
  const draft = data();
  const {node} = create(draft, 'bow', draft.upgrades[2]);
  draft.upgrades.push(node);
  assert.equal(node.treeRow, 2);
  assert.equal(node.requires, 'multi_shot');
  assert.equal(validate(draft, attributes), '');
  assert.notEqual(create(draft, 'bow', node).node.id, node.id);
  node.effects.push({attribute:'fire_rate', operation:'multiply', values:[1,.8]});
  assert.equal(validate(draft, attributes), '');
});
test('broken references, duplicate setters and non-neutral locked effects are rejected', () => {
  const draft = data();
  const {node} = create(draft, 'bow', draft.upgrades[2]);
  draft.upgrades.push(node);
  node.requiredLevel = 99;
  assert.match(validate(draft, attributes), /前置/);
  node.requiredLevel = 1;
  node.effects[0].values[0] = 1;
  assert.match(validate(draft, attributes), /未升级/);
  node.effects[0].operation = 'set';
  assert.match(validate(draft, attributes), /只能/);
});
