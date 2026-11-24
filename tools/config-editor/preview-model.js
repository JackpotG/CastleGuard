// Mirrors the spawn-only random choices from MainScene; combat still determines wave completion.
const PreviewModel = (() => {
  const laneCount = 12;
  const stageWidth = 720;
  const stageHeight = 1280;

  function position(event) {
    const targetX = (event.lane + .5 + event.jitter) / laneCount;
    return {
      startX: event.from === "left" ? -20 / stageWidth : event.from === "right" ? 1 + 20 / stageWidth : targetX,
      startY: event.from === "spawn" ? .04 : .77 - event.sideHeight * .68,
      targetX,
      targetY: .82,
    };
  }

  function travelDuration(event) {
    const speed = event.configuredSpeed > 0 ? event.configuredSpeed : event.baselineSpeed * event.speedFactor;
    if (!(speed > 0)) return 6 / event.speedFactor;
    const {startX, startY, targetX, targetY} = position(event);
    return Math.hypot((targetX - startX) * stageWidth, (targetY - startY) * stageHeight) / speed;
  }

  function buildWave(wave, {randomize = false, random = Math.random, baseSpeed = 0, monsterSpeeds = {}} = {}) {
    const events = [];
    wave.groups.forEach((group, groupIndex) => {
      const types = group.types || Array.from({length: Math.max(0, group.count || 0)}, () => group.type);
      const formation = group.formation || "scatter";
      const from = group.from || "spawn";
      const delay = Math.max(0, Number(group.delay) || 0);
      const interval = Math.max(0, Number(group.interval) || 1);
      const rowStart = group.lane >= 0 ? group.lane : Math.floor((laneCount - Math.min(types.length, laneCount)) / 2);
      const columnLane = group.lane >= 0 ? group.lane % laneCount :
        randomize && formation === "column" ? Math.floor(random() * laneCount) : (groupIndex * 7 + 5) % laneCount;
      types.forEach((type, index) => {
        const batch = Math.floor(index / laneCount);
        const lane = formation === "row" ? rowStart + index % laneCount :
          formation === "column" ? columnLane :
          group.lane >= 0 ? group.lane % laneCount :
          randomize ? Math.floor(random() * laneCount) : (groupIndex * 7 + index * 5 + 2) % laneCount;
        const jitter = randomize && from === "spawn" && formation === "scatter" ? (random() - .5) * .56 : 0;
        const sideHeight = from !== "spawn" && randomize ? random() : .5;
        const configuredSpeed = group.useConfiguredSpeed ? Number(group.configuredSpeed) : null;
        events.push({
          time: delay + (formation === "row" ? batch : index) * interval,
          type, lane, from, groupIndex, jitter, sideHeight, configuredSpeed,
          baselineSpeed: monsterSpeeds[type] || baseSpeed,
          speedFactor: configuredSpeed > 0 || !randomize ? 1 : .85 + random() * .3,
        });
      });
    });
    events.sort((a, b) => a.time - b.time || a.groupIndex - b.groupIndex);
    return {events, duration: Math.max(6, ...events.map((event) => event.time + travelDuration(event)))};
  }

  return {buildWave, position, travelDuration};
})();
if (typeof module !== "undefined") module.exports = PreviewModel;
