const assert = require("node:assert/strict");
const test = require("node:test");
const {buildWave, position, travelDuration} = require("./preview-model.js");

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
  assert.equal(wave.events[0].jitter, 0);
  assert.equal(wave.events[0].speedFactor, 1);
  assert.equal(wave.duration, 10);
});

test("random scatter redraws lane, lateral jitter, and individual speed", () => {
  const samples = [0, .25, .5, .99, .75, 0];
  const wave = buildWave({groups: [{type: "normal", count: 2, interval: 1}]}, {
    randomize: true, random: () => samples.shift(),
  });
  assert.deepEqual(wave.events.map(({lane, jitter, speedFactor}) => [lane, jitter, speedFactor]), [
    [0, -.14, 1], [11, .14, .85],
  ]);
  assert.ok(wave.duration > 8);
});

test("random column shares a lane while fixed speed bypasses speed variance", () => {
  const samples = [.9];
  const wave = buildWave({groups: [{types: ["normal", "fast"], formation: "column", interval: 1,
    useConfiguredSpeed: true, configuredSpeed: 35}]}, {randomize: true, random: () => samples.shift(), baseSpeed: 70});
  assert.deepEqual(wave.events.map(({lane, jitter, speedFactor}) => [lane, jitter, speedFactor]), [
    [10, 0, 1], [10, 0, 1],
  ]);
  assert.ok(Math.abs(wave.duration - (1 + 998.4 / 35)) < .001);
  assert.equal(samples.length, 0);
});

test("type baseline speeds change travel time without changing spawn timing", () => {
  const wave = buildWave({groups: [{types: ["normal", "fast"], formation: "row"}]}, {
    baseSpeed: 70, monsterSpeeds: {normal: 70, fast: 126},
  });
  assert.deepEqual(wave.events.map(({time}) => time), [0, 0]);
  assert.equal(wave.events[0].baselineSpeed, 70);
  assert.equal(wave.events[1].baselineSpeed, 126);
  assert.ok(Math.abs(wave.duration - 998.4 / 70) < .001);
  assert.ok(Math.abs(travelDuration(wave.events[0]) - 998.4 / 70) < .001);
  assert.ok(Math.abs(travelDuration(wave.events[1]) - 998.4 / 126) < .001);
  assert.deepEqual(position(wave.events[0]), {
    startX: 5.5 / 12, startY: .04, targetX: 5.5 / 12, targetY: .82,
  });
});

test("side entrance samples its height separately for each monster", () => {
  const samples = [0, .2, .4, .5, .8, .6];
  const wave = buildWave({groups: [{type: "fast", count: 2, from: "left"}]}, {
    randomize: true, random: () => samples.shift(),
  });
  assert.deepEqual(wave.events.map(({lane, jitter, sideHeight, speedFactor}) =>
    [lane, jitter, sideHeight, Number(speedFactor.toFixed(2))]), [
    [0, 0, .2, .97], [6, 0, .8, 1.03],
  ]);
  assert.ok(Math.abs(position(wave.events[0]).startY - .634) < .001);
  assert.ok(travelDuration({...wave.events[0], baselineSpeed: 70}) > 0);
});
