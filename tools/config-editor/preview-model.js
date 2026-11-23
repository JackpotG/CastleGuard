// Mirrors the spawn-only random choices from MainScene; combat still determines wave completion.
const PreviewModel = (() => {
  const laneCount = 12;

  function travelDuration(event) {
    const speed = event.configuredSpeed > 0 ? event.configuredSpeed : event.baselineSpeed * event.speedFactor;
    return speed > 0 && event.referenceSpeed > 0 ? Math.max(1.5, Math.min(20, 6 * event.referenceSpeed / speed)) :
      6 / event.speedFactor;
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
          referenceSpeed: baseSpeed, baselineSpeed: monsterSpeeds[type] || 0,
          speedFactor: configuredSpeed > 0 || !randomize ? 1 : .85 + random() * .3,
        });
      });
    });
    events.sort((a, b) => a.time - b.time || a.groupIndex - b.groupIndex);
    return {events, duration: Math.max(6, ...events.map((event) => event.time + travelDuration(event)))};
  }

  return {buildWave, travelDuration};
})();
if (typeof module !== "undefined") module.exports = PreviewModel;
