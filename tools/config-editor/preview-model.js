// A deterministic view of spawn timing; battle completion still depends on combat.
const PreviewModel = (() => {
  const laneCount = 12;

  function buildWave(wave) {
    const events = [];
    wave.groups.forEach((group, groupIndex) => {
      const types = group.types || Array.from({length: Math.max(0, group.count || 0)}, () => group.type);
      const formation = group.formation || "scatter";
      const delay = Math.max(0, Number(group.delay) || 0);
      const interval = Math.max(0, Number(group.interval) || 1);
      const rowStart = group.lane >= 0 ? group.lane : Math.floor((laneCount - Math.min(types.length, laneCount)) / 2);
      const columnLane = group.lane >= 0 ? group.lane % laneCount : (groupIndex * 7 + 5) % laneCount;
      types.forEach((type, index) => {
        const batch = Math.floor(index / laneCount);
        const lane = formation === "row" ? rowStart + index % laneCount :
          formation === "column" ? columnLane :
          group.lane >= 0 ? group.lane % laneCount : (groupIndex * 7 + index * 5 + 2) % laneCount;
        events.push({
          time: delay + (formation === "row" ? batch : index) * interval,
          type, lane, from: group.from || "spawn", groupIndex,
          configuredSpeed: group.useConfiguredSpeed ? Number(group.configuredSpeed) : null,
        });
      });
    });
    events.sort((a, b) => a.time - b.time || a.groupIndex - b.groupIndex);
    return {events, duration: Math.max(6, (events.at(-1)?.time || 0) + 6)};
  }

  return {buildWave};
})();
if (typeof module !== "undefined") module.exports = PreviewModel;
