const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const {normalize, validate, validateLayout, create} = require('./upgrade-editor.js');
const {plan} = require('./tree-position.js');
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
  assert.deepEqual(node.prerequisites, [{id: 'multi_shot', level: 1}]);
  assert.equal(validate(draft, attributes), '');
  assert.notEqual(create(draft, 'bow', node).node.id, node.id);
  node.effects.push({attribute:'fire_rate', operation:'multiply', values:[1,.8]});
  assert.equal(validate(draft, attributes), '');
});
test('broken references, duplicate setters and non-neutral locked effects are rejected', () => {
  const draft = data();
  const {node} = create(draft, 'bow', draft.upgrades[2]);
  draft.upgrades.push(node);
  node.prerequisites[0].level = 99;
  assert.match(validate(draft, attributes), /前置/);
  node.prerequisites[0].level = 1;
  node.effects[0].values[0] = 1;
  assert.match(validate(draft, attributes), /未升级/);
  node.effects[0].operation = 'set';
  assert.match(validate(draft, attributes), /只能/);
});

test('multiple prerequisites retain their own levels and reject duplicates and missing parents', () => {
  const draft = data();
  const {node} = create(draft, 'bow', draft.upgrades.find((n) => n.id === 'precision'));
  node.prerequisites.push({id: 'heavy_strike', level: 1});
  draft.upgrades.push(node);
  assert.equal(validate(draft, attributes), '');
  assert.equal('requires' in draft.upgrades[2], false);
  assert.deepEqual(draft.upgrades[2].prerequisites, [{id: 'fire_rate', level: 2}]);
  const saved = normalize(JSON.parse(JSON.stringify(draft)));
  assert.deepEqual(saved.upgrades.at(-1).prerequisites, node.prerequisites);
  node.prerequisites.push({id: 'heavy_strike', level: 1});
  assert.match(validate(draft, attributes), /前置/);
  node.prerequisites.at(-1).id = 'missing';
  assert.match(validate(draft, attributes), /前置/);
  node.prerequisites.pop();
  node.prerequisites[1].level = 2;
  assert.match(validate(draft, attributes), /前置/);
});
test('a cycle on the second incoming edge is rejected for defense nodes', () => {
  const draft = data();
  const a = create(draft, 'defense', draft.upgrades.find((node) => node.id === 'wall_health')).node; draft.upgrades.push(a);
  const b = create(draft, 'defense', a).node; draft.upgrades.push(b);
  a.prerequisites = [{id: 'wall_health', level: 1}, {id: b.id, level: 1}];
  b.prerequisites = [{id: a.id, level: 1}];
  assert.match(validate(draft, attributes), /循环/);
});

for (const track of ['bow', 'magic', 'defense']) {
  test(`${track} uses common node creation, AND prerequisites, placement and validation`, () => {
    const draft = normalize({upgrades: [], skills: []});
    const first = create(draft, track).node; draft.upgrades.push(first);
    const second = create(draft, track).node; draft.upgrades.push(second);
    first.treeX = .27; second.treeX = .73;
    first.costs.push(20); first.effects[0].values.push(2);
    const child = create(draft, track, first).node;
    child.prerequisites = [{id: first.id, level: 2}, {id: second.id, level: 1}];
    child.treeX = .3; draft.upgrades.push(child);
    assert.equal(child.track, track);
    assert.equal(child.treeRow, 1);
    for (const [id, x] of plan(draft.upgrades, child, 'parent-align').updates)
      draft.upgrades.find((node) => node.id === id).treeX = x;
    assert.equal(child.treeX, .5);
    assert.equal(validate(draft, attributes), '');
    const saved = normalize(JSON.parse(JSON.stringify(draft)));
    assert.deepEqual(saved, draft);
    child.treeRow = 0;
    assert.match(validate(draft, attributes), /前置/);
    child.treeRow = 1; child.treeX = .99;
    assert.match(validate(draft, attributes), /位置/);
  });
}

test('legacy defense migration is deterministic and preserves IDs, effects, costs and prerequisites', () => {
  const legacy = JSON.parse(fs.readFileSync(new URL(root + '../../Tests/fixtures/legacy-defense-tree.json', `file://${__filename}`)));
  const draft = normalize(structuredClone(legacy));
  assert.equal(validate(draft, attributes), '');
  const positions = Object.fromEntries(draft.upgrades.map((node) => [node.id, [node.track,node.treeRow,node.treeX]]));
  assert.deepEqual(positions, {wall_health:['defense',0,.73],wall_armor:['defense',0,.27],wall_training:['defense',1,.5]});
  assert.deepEqual(draft.upgrades[0].costs, legacy.upgrades[0].costs);
  assert.deepEqual(draft.upgrades[0].effects[0].values, legacy.upgrades[0].values);
  assert.deepEqual(draft.upgrades[2].prerequisites, legacy.upgrades[2].prerequisites);
  assert.deepEqual(normalize(structuredClone(draft)), draft);
  const reverse = normalize({upgrades: [...legacy.upgrades].reverse(),skills:[]});
  assert.deepEqual(Object.fromEntries(reverse.upgrades.map((node) => [node.id,node.treeX])),
                   Object.fromEntries(draft.upgrades.map((node) => [node.id,node.treeX])));
});

test('active spells use the same layout limits, presets and earlier-parent rule', () => {
  const nodes = [{id:'first',treeRow:0,treeX:.5,prerequisites:[]},
    {id:'later',treeRow:9,treeX:.3,prerequisites:[{id:'first',level:1}]}];
  assert.equal(validateLayout(nodes), '');
  assert.deepEqual(plan(nodes,nodes[1],'parent-align'),{updates:[['later',.5]]});
  nodes[1].treeRow=17; assert.match(validateLayout(nodes),/位置/);
  nodes[1].treeRow=0; nodes[1].treeX=.8; assert.match(validateLayout(nodes),/前置/);
});
