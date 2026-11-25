const assert = require("node:assert/strict");
const test = require("node:test");
const {plan} = require("./tree-position.js");

test("centers a parent between its direct children", () => {
  const nodes = [
    {id: "parent", treeRow: 0, treeX: .35},
    {id: "left", requires: "parent", treeRow: 1, treeX: .2},
    {id: "right", requires: "parent", treeRow: 1, treeX: .6},
    {id: "grandchild", requires: "left", treeRow: 2, treeX: .8},
  ];
  assert.deepEqual(plan(nodes, nodes[0], "children-center"), {updates: [["parent", .4]]});
});

test("distributes a row across usable width in its existing order", () => {
  const nodes = [
    {id: "middle", treeRow: 1, treeX: .6},
    {id: "first", treeRow: 1, treeX: .2},
    {id: "last", treeRow: 1, treeX: .8},
    {id: "other-row", treeRow: 0, treeX: .3},
  ];
  assert.deepEqual(plan(nodes, nodes[0], "row-distribute"), {
    updates: [["first", .12], ["middle", .5], ["last", .88]],
  });
  assert.equal(nodes[0].treeX, .6);
  assert.deepEqual(plan([nodes[0]], nodes[0], "row-distribute"), {updates: [["middle", .5]]});
});

test("aligns with a parent and rejects collisions without changing nodes", () => {
  const nodes = [
    {id: "parent", treeRow: 0, treeX: .7},
    {id: "child", requires: "parent", treeRow: 1, treeX: .2},
    {id: "sibling", treeRow: 1, treeX: .58},
  ];
  assert.match(plan(nodes, nodes[1], "parent-align").error, /过近/);
  assert.equal(nodes[1].treeX, .2);
  nodes[2].treeX = .36;
  assert.deepEqual(plan(nodes, nodes[1], "parent-align"), {updates: [["child", .7]]});
  assert.match(plan(nodes, nodes[1], "canvas-center").error, /过近/);
});

test("rejects unavailable relations and rows too crowded to distribute", () => {
  const nodes = Array.from({length: 6}, (_, index) => ({id: String(index), treeRow: 0, treeX: .12 + index * .15}));
  assert.match(plan(nodes, nodes[0], "children-center").error, /没有直接子节点/);
  assert.match(plan(nodes, nodes[0], "parent-align").error, /没有前置节点/);
  assert.match(plan(nodes, nodes[0], "row-distribute").error, /过多/);
});

test("multi-parent layout aligns with all parents and finds shared children", () => {
  const nodes = [
    {id: "left", treeRow: 0, treeX: .2},
    {id: "right", treeRow: 0, treeX: .8},
    {id: "child", prerequisites: [{id:"left",level:2},{id:"right",level:1}], treeRow:1, treeX:.4},
    {id: "other", prerequisites: [{id:"right",level:1}], treeRow:1, treeX:.8},
  ];
  assert.deepEqual(plan(nodes, nodes[2], "parent-align"), {updates:[["child", .5]]});
  assert.deepEqual(plan(nodes, nodes[1], "children-center"), {updates:[["right", .6000000000000001]]});
});
