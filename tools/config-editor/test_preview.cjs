const assert = require("node:assert/strict");
const test = require("node:test");
const {buildWave} = require("./preview-model.js");

test("row spawns mixed types together in adjacent lanes", () => {
  const wave = buildWave({groups: [{types: ["normal", "fast", "shield"], formation: "row", lane: 4, delay: 2, interval: 9}]});
  assert.deepEqual(wave.events.map(({time, type, lane}) => [time, type, lane]), [
    [2, "normal", 4], [2, "fast", 5], [2, "shield", 6],
  ]);
});

test("column spawns a mixed sequence in one lane at configured intervals", () => {
  const wave = buildWave({groups: [{types: ["normal", "ranged", "fast"], formation: "column", delay: 1.5, interval: .75, useConfiguredSpeed: true, configuredSpeed: 80}]});
  assert.deepEqual(wave.events.map(({time, type, lane}) => [time, type, lane]), [
    [1.5, "normal", 5], [2.25, "ranged", 5], [3, "fast", 5],
  ]);
  assert.ok(wave.events.every((event) => event.configuredSpeed === 80));
});

test("groups overlap independently and default values remain valid", () => {
  const wave = buildWave({groups: [
    {type: "normal", count: 3, interval: 2},
    {type: "fast", count: 2, delay: 1, interval: 1},
  ]});
  assert.deepEqual(wave.events.map(({time, type}) => [time, type]), [
    [0, "normal"], [1, "fast"], [2, "normal"], [2, "fast"], [4, "normal"],
  ]);
  assert.equal(wave.events[0].configuredSpeed, null);
  assert.equal(wave.duration, 10);
});
