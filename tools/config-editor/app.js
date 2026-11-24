const $ = (selector) => document.querySelector(selector);
const escapeHtml = (value) => String(value ?? "").replace(/[&<>"']/g, (character) => ({
  "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;",
})[character]);
const clone = (value) => structuredClone(value);
const state = {
  mode: "levels", documents: {}, dirty: {levels: false, tree: false, monsters: false},
  level: 0, wave: 0, group: 0, track: "bow", node: "arrow_damage", monster: "normal",
  balanceCategory: "monsters", baseUpgrade: "arrow_damage", search: "",
};
const previewState = {open: false, wave: 0, time: 0, speed: 1, randomize: false, playing: false, frame: 0, lastFrame: 0, timeline: null};
const trackNames = {bow: "军械所", magic: "研究院", defense: "城防营", spells: "法术研习"};
const formationNames = {scatter: "散列", row: "横排", column: "长列"};
const originNames = {spawn: "战场上沿", left: "左侧翼", right: "右侧翼"};
const monsterNames = {
  normal: "普通", fast: "快速", ranged: "远程", shield: "盾兵", bomber: "自爆",
  flyer: "飞行", healer: "治疗", splitter: "分裂", rager: "狂暴", ghost: "幽灵",
  summoner: "召唤", aegis: "护符", boss: "首领",
};
let toastTimer;

function activeDocumentKind() { return state.mode === "balance" ? state.balanceCategory : state.mode; }
function documentData() { return state.documents[activeDocumentKind()]?.data; }
function currentLevel() { return state.documents.levels.data.levels[state.level]; }
function currentWave() { return currentLevel().waves[state.wave]; }
function currentGroup() { return currentWave().groups[state.group]; }
function trackOf(upgrade) { return upgrade.track || "defense"; }
function currentNode() {
  const data = state.documents.tree.data;
  const list = state.track === "spells" ? data.skills : data.upgrades;
  return list.find((entry) => entry.id === state.node);
}
function currentBaseUpgrade() {
  return state.documents.tree.data.upgrades.find((entry) => entry.id === state.baseUpgrade);
}
function monsterSpeeds() {
  const data = state.documents.monsters.data;
  return Object.fromEntries(Object.entries(data.monsters).map(([type, attributes]) =>
    [type, Math.round(data.baseSpeed * attributes.speedMultiplier * 10000) / 10000]));
}
function monsterTypes() {
  const types = new Set();
  for (const level of state.documents.levels.data.levels)
    for (const wave of level.waves)
      for (const group of wave.groups)
        for (const type of group.types || [group.type]) types.add(type);
  return [...types].filter(Boolean).sort();
}
function speedReferences(group) {
  const speeds = monsterSpeeds();
  return [...new Set(group.types || [group.type])].map((type) => ({
    type, speed: speeds[type],
  })).filter(({speed}) => positive(speed));
}
function groupSpeedLabel(group) {
  return group.useConfiguredSpeed ?
    positive(group.configuredSpeed) ? `${group.configuredSpeed} 像素/秒` : "指定移速待填写" : "类型速度";
}
function showToast(message, error = false) {
  const toast = $("#toast");
  toast.textContent = message;
  toast.className = `toast show${error ? " error" : ""}`;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toast.className = "toast"; }, 3500);
}
function markDirty() {
  state.dirty[activeDocumentKind()] = true;
  const dirty = Object.values(state.dirty).some(Boolean);
  $("#save-state").textContent = dirty ? "有未保存修改" : "已同步";
  $("#save-state").classList.toggle("dirty", dirty);
}
async function loadAll() {
  if (previewState.open) closePreview();
  const [levels, tree, monsters] = await Promise.all(
    ["levels", "tree", "monsters"].map(async (kind) => {
      const response = await fetch(`/api/${kind}`);
      if (!response.ok) {
        const problem = await response.json().catch(() => ({}));
        throw new Error(problem.error || `${kind} 加载失败`);
      }
      return response.json();
    }),
  );
  state.documents = {levels, tree, monsters};
  state.dirty = {levels: false, tree: false, monsters: false};
  state.level = 0; state.wave = 0; state.group = 0;
  state.track = "bow"; state.node = tree.data.upgrades.find((item) => trackOf(item) === "bow")?.id;
  state.monster = "normal"; state.balanceCategory = "monsters"; state.baseUpgrade = "arrow_damage";
  markClean();
  render();
}
function markClean() {
  const dirty = Object.values(state.dirty).some(Boolean);
  $("#save-state").textContent = dirty ? "有未保存修改" : "已同步";
  $("#save-state").classList.toggle("dirty", dirty);
}
async function saveCurrent() {
  const errors = validateDraft();
  if (errors.length) { showToast(errors[0], true); return; }
  const kind = activeDocumentKind();
  const document = state.documents[kind];
  try {
    const response = await fetch(`/api/${kind}`, {
      method: "PUT", headers: {"Content-Type": "application/json"},
      body: JSON.stringify({revision: document.revision, data: document.data}),
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || "保存失败");
    document.revision = result.revision;
    state.dirty[kind] = false;
    markClean();
    showToast("配置已保存");
  } catch (error) { showToast(error.message, true); }
}
function positive(value) { return typeof value === "number" && Number.isFinite(value) && value > 0; }
function validateDraft() {
  const data = documentData();
  if (activeDocumentKind() === "monsters") return [];
  if (activeDocumentKind() === "levels") {
    if (!data.levels.length) return ["至少需要一关"];
    for (const [li, level] of data.levels.entries()) {
      const prefix = `第 ${li + 1} 关`;
      if (!level.name?.trim() || !Number.isInteger(level.clearCrystals) || level.clearCrystals < 0 || !level.waves.length)
        return [`${prefix}：名称、通关水晶或波次无效`];
      for (const [wi, wave] of level.waves.entries()) {
        if (!wave.name?.trim() || !wave.groups.length || (wave.restAfter !== undefined &&
            (!Number.isFinite(wave.restAfter) || wave.restAfter < 0)))
          return [`${prefix}第 ${wi + 1} 波无效`];
        for (const [gi, group] of wave.groups.entries()) {
          const where = `${prefix}第 ${wi + 1} 波第 ${gi + 1} 组`;
          const count = group.types ? group.types.length : group.count;
          if (!Number.isInteger(count) || count < 1 || (group.types ? !group.types.every(Boolean) : !group.type))
            return [`${where}怪物列表为空`];
          if (group.interval !== undefined && !positive(group.interval)) return [`${where}出怪间隔须大于 0`];
          if (group.delay !== undefined && (!Number.isFinite(group.delay) || group.delay < 0))
            return [`${where}延迟不能为负数`];
          if (group.lane !== undefined && (!Number.isInteger(group.lane) || group.lane < -1))
            return [`${where}起始车道无效`];
          if (group.formation === "row" && (count > 12 || (group.lane ?? -1) > 12 - count))
            return [`${where}横排超出 12 条车道`];
          if ((group.formation || "scatter") !== "scatter" && (group.from || "spawn") !== "spawn")
            return [`${where}编队只能从战场上沿出现`];
          if (group.configuredSpeed !== undefined && !positive(group.configuredSpeed))
            return [`${where}指定移速须大于 0`];
          if (group.useConfiguredSpeed && !positive(group.configuredSpeed))
            return [`${where}需要填写正数移速`];
        }
      }
    }
    return [];
  }
  const upgradeIds = new Set(data.upgrades.map((item) => item.id));
  const skillIds = new Set(data.skills.map((item) => item.id));
  for (const node of data.upgrades) {
    if (!node.name?.trim() || !node.description?.trim() || !node.costs.length ||
        node.values.length !== node.costs.length + 1 || !node.costs.every((cost) => Number.isInteger(cost) && cost > 0) ||
        !node.values.every((value) => Number.isFinite(value) && (value > 0 ||
          (["precision", "mana_flow", "wall_armor"].includes(node.id) && value === 0))))
      return [`${node.id} 的价格或数值无效`];
    if (node.requires && (!upgradeIds.has(node.requires) || node.requires === node.id))
      return [`${node.id} 的前置节点无效`];
    if (node.track && (!Number.isInteger(node.treeRow) || node.treeRow < 0 || node.treeRow > 4 ||
        !Number.isFinite(node.treeX) || node.treeX < .12 || node.treeX > .88))
      return [`${node.id} 的树位置无效`];
    if (node.track && !node.requires && node.treeRow !== 0)
      return [`${node.id} 没有前置节点，应位于首层`];
    if (!node.requires && (node.requiredLevel ?? 0) !== 0)
      return [`${node.id} 没有前置节点，前置等级应为 0`];
    if (node.track && data.upgrades.some((other) => other !== node && other.track === node.track &&
        other.treeRow === node.treeRow && Math.abs(other.treeX - node.treeX) < .18))
      return [`${node.id} 与同层节点过近`];
    if (node.requires && node.track) {
      const parent = data.upgrades.find((item) => item.id === node.requires);
      if (parent.track !== node.track || parent.treeRow >= node.treeRow ||
          !Number.isInteger(node.requiredLevel) || node.requiredLevel < 1 || node.requiredLevel > parent.costs.length)
        return [`${node.id} 的前置等级或层级无效`];
    }
  }
  for (const node of data.skills) {
    if (!node.name?.trim() || !node.description?.trim() || !Number.isInteger(node.treeRow) ||
        node.treeRow < 0 || node.treeRow > 8 || !Number.isFinite(node.treeX) || node.treeX < .12 || node.treeX > .88 ||
        !Number.isInteger(node.unlockCost) || node.unlockCost < 0 ||
        !Number.isInteger(node.unlockStage) || node.unlockStage < 0 ||
        (node.defaultUnlocked && (node.unlockCost > 0 || node.unlockStage > 0)) ||
        (!node.defaultUnlocked && (node.unlockCost > 0) === (node.unlockStage > 0)))
      return [`${node.id} 的解锁条件或树位置无效`];
    if (data.skills.some((other) => other !== node && other.treeRow === node.treeRow &&
        Math.abs(other.treeX - node.treeX) < .18))
      return [`${node.id} 与同层法术过近`];
    if (node.requires && (!skillIds.has(node.requires) ||
        data.skills.find((item) => item.id === node.requires).treeRow >= node.treeRow))
      return [`${node.id} 的前置法术无效`];
    if (node.requires && node.unlockStage > 0) {
      const parent = data.skills.find((item) => item.id === node.requires);
      if (!parent.defaultUnlocked && (parent.unlockStage <= 0 || parent.unlockStage > node.unlockStage))
        return [`${node.id} 的解锁关卡早于前置法术`];
    }
  }
  return [];
}
function options(values, selected, labels = {}) {
  return values.map((value) => `<option value="${escapeHtml(value)}"${String(value) === String(selected) ? " selected" : ""}>${escapeHtml(labels[value] || value)}</option>`).join("");
}
function field(scope, name, label, value, type = "text", extra = "") {
  const attribute = `data-scope="${scope}" data-field="${name}"`;
  if (type === "textarea") return `<div class="field ${extra}"><label>${label}</label><textarea ${attribute}>${escapeHtml(value)}</textarea></div>`;
  if (type === "select") return `<div class="field ${extra}"><label>${label}</label><select ${attribute}>${value}</select></div>`;
  return `<div class="field ${extra}"><label>${label}</label><input ${attribute} type="${type}" value="${escapeHtml(value)}" ${type === "number" ? 'step="any"' : ""}></div>`;
}
function monsterField(scope, name, label, value, minimum, maximum, step = "any") {
  return `<div class="field"><label for="monster-${scope}-${name}">${label}</label><input id="monster-${scope}-${name}" data-scope="${scope}" data-field="${name}" type="number" value="${escapeHtml(value)}" min="${minimum}" max="${maximum}" step="${step}" title="允许范围：${minimum}–${maximum}"></div>`;
}
function render() {
  $("#mode-levels").classList.toggle("active", state.mode === "levels");
  $("#mode-tree").classList.toggle("active", state.mode === "tree");
  $("#mode-balance").classList.toggle("active", state.mode === "balance");
  if (!documentData()) return;
  renderSidebar();
  if (state.mode === "levels") renderLevels();
  else if (state.mode === "tree") renderTree();
  else renderBalance();
}
function renderSidebar() {
  const head = $("#sidebar-head");
  const list = $("#sidebar-list");
  if (state.mode === "levels") {
    const levels = documentData().levels;
    head.innerHTML = `<div class="line"><p class="sidebar-title grow">关卡 · ${levels.length}</p><button class="small-button" data-action="add-level" title="复制当前关卡" aria-label="复制当前关卡">+</button></div><input id="level-search" class="search-input" type="search" placeholder="搜索关卡" value="${escapeHtml(state.search)}">`;
    list.innerHTML = levels.map((level, index) => ({level, index})).filter(({level, index}) =>
      `${index + 1} ${level.name}`.toLowerCase().includes(state.search.toLowerCase())).map(({level, index}) => {
      const count = level.waves.reduce((sum, wave) => sum + wave.groups.reduce((part, group) => part + (group.types?.length || group.count || 0), 0), 0);
      return `<button class="sidebar-item ${index === state.level ? "selected" : ""}" data-action="select-level" data-index="${index}"><strong>${String(index + 1).padStart(2, "0")} · ${escapeHtml(level.name)}</strong><small>${level.waves.length} 波 · ${count} 怪</small></button>`;
    }).join("");
    return;
  }
  if (state.mode === "balance") {
    head.innerHTML = `<p class="sidebar-title">基础数值</p><div class="segment"><button data-action="select-balance-category" data-category="monsters" class="${state.balanceCategory === "monsters" ? "active" : ""}">怪物</button><button data-action="select-balance-category" data-category="tree" class="${state.balanceCategory === "tree" ? "active" : ""}">成长初始值</button></div>`;
    if (state.balanceCategory === "monsters") {
      const speeds = monsterSpeeds();
      list.innerHTML = Object.entries(monsterNames).map(([id, name]) => `<button class="sidebar-item ${id === state.monster ? "selected" : ""}" data-action="select-monster" data-id="${id}"><strong>${name}</strong><small>${id} · ${speeds[id]} 像素/秒</small></button>`).join("");
    } else {
      const upgrades = documentData().upgrades;
      list.innerHTML = ["bow", "magic", "defense"].map((track) => `<p class="sidebar-group-title">${trackNames[track]}</p>${upgrades.filter((entry) => trackOf(entry) === track).map((entry) => `<button class="sidebar-item ${entry.id === state.baseUpgrade ? "selected" : ""}" data-action="select-base-upgrade" data-id="${escapeHtml(entry.id)}"><strong>${escapeHtml(entry.name)}</strong><small>初始 ${escapeHtml(entry.values[0])}</small></button>`).join("")}`).join("");
    }
    return;
  }
  const data = documentData();
  head.innerHTML = `<p class="sidebar-title">成长体系</p><div class="segment">${Object.entries(trackNames).map(([track, label]) => `<button class="${track === state.track ? "active" : ""}" data-action="select-track" data-track="${track}">${label}</button>`).join("")}</div>`;
  const nodes = state.track === "spells" ? data.skills : data.upgrades.filter((item) => trackOf(item) === state.track);
  list.innerHTML = nodes.map((node) => `<button class="sidebar-item ${node.id === state.node ? "selected" : ""}" data-action="select-node" data-id="${escapeHtml(node.id)}"><strong>${escapeHtml(node.name)}</strong><small>${escapeHtml(node.id)}${node.treeRow >= 0 ? ` · 第 ${node.treeRow + 1} 层` : ""}</small></button>`).join("");
}
function renderLevels() {
  const level = currentLevel();
  const wave = currentWave();
  const count = level.waves.reduce((sum, item) => sum + item.groups.reduce((n, group) => n + (group.types?.length || group.count || 0), 0), 0);
  $("#main-content").innerHTML = `
    <div class="page-head"><div><p class="eyebrow">CAMPAIGN / ${String(state.level + 1).padStart(2, "0")}</p><h1>${escapeHtml(level.name)}</h1><div class="meta-row"><span><strong>${level.waves.length}</strong> 波次</span><span><strong>${count}</strong> 怪物</span><span><strong>${level.clearCrystals}</strong> 水晶</span></div></div><div class="line"><button class="primary-button" data-action="open-preview" type="button">预览关卡</button><button class="small-button danger-button" data-action="delete-level" title="删除当前关卡" aria-label="删除当前关卡">×</button></div></div>
    <div class="field-grid">${field("level", "name", "关卡名称", level.name)}${field("level", "clearCrystals", "通关水晶", level.clearCrystals, "number")}</div>
    <h2 class="section-title">波次 <span class="section-actions"><button class="small-button" data-action="add-wave" title="添加波次" aria-label="添加波次">+</button></span></h2>
    <div class="wave-list">${level.waves.map((item, index) => `<button class="wave-tab ${index === state.wave ? "selected" : ""}" data-action="select-wave" data-index="${index}">${escapeHtml(item.name)}<small>${item.groups.length}</small></button>`).join("")}</div>
    <div class="field-grid">${field("wave", "name", "波次名称", wave.name)}${field("wave", "restAfter", "清场休整 · 秒", wave.restAfter ?? 3, "number")}</div>
    <div class="toolbar"><button class="small-button" data-action="move-wave-up" title="波次上移" aria-label="波次上移">↑</button><button class="small-button" data-action="move-wave-down" title="波次下移" aria-label="波次下移">↓</button><button class="small-button" data-action="copy-wave" title="复制波次" aria-label="复制波次">⧉</button><div class="spacer"></div><button class="small-button danger-button" data-action="delete-wave" title="删除波次" aria-label="删除波次">×</button></div>
    <h2 class="section-title">出怪组 <span class="section-actions"><button class="small-button" data-action="add-group" title="添加出怪组" aria-label="添加出怪组">+</button></span></h2>
    ${wave.groups.map((group, index) => `<button class="group-row ${index === state.group ? "selected" : ""}" data-action="select-group" data-index="${index}"><span class="group-index">${String(index + 1).padStart(2, "0")}</span><span><strong>${escapeHtml((group.types || [group.type]).map((type) => monsterNames[type] || type).join(" · "))}</strong><br><small>${formationNames[group.formation || "scatter"]} · ${group.types?.length || group.count} 只 · 延迟 ${group.delay ?? 0}s</small></span>${group.useConfiguredSpeed ? '<span class="badge speed">同速</span>' : '<span class="badge">类型速度</span>'}</button>`).join("")}
    <h2 class="section-title">编队预览</h2>${renderFormationPreview(currentGroup())}`;
  renderGroupInspector();
}
function renderFormationPreview(group) {
  const types = group.types || Array.from({length: Math.min(group.count || 0, 12)}, () => group.type);
  const formation = group.formation || "scatter";
  if (formation === "column") return `<div class="preview"><div class="muted">第 ${group.lane >= 0 ? group.lane + 1 : "随机"} 车道 · 间隔 ${group.interval ?? 1}s · ${groupSpeedLabel(group)}</div><div class="column-preview">${types.map((type, index) => `<span class="column-token">${escapeHtml(monsterNames[type] || type)}</span>${index < types.length - 1 ? '<span class="arrow">→</span>' : ""}`).join("")}</div></div>`;
  const start = group.lane >= 0 ? group.lane : Math.floor((12 - types.length) / 2);
  return `<div class="preview"><div class="muted">${formation === "row" ? "同帧横排" : "散列出生"} · ${groupSpeedLabel(group)}</div><div class="lane-grid">${Array.from({length: 12}, (_, lane) => {
    const index = formation === "row" ? lane - start : -1;
    return `<div><div class="lane ${index >= 0 && index < types.length ? "occupied" : ""}">${index >= 0 && index < types.length ? escapeHtml((monsterNames[types[index]] || types[index]).slice(0, 2)) : ""}</div><div class="lane-number">${lane + 1}</div></div>`;
  }).join("")}</div></div>`;
}
function renderGroupInspector() {
  const group = currentGroup();
  const mixed = Array.isArray(group.types);
  const typeOptions = monsterTypes();
  const references = speedReferences(group);
  const speedReference = references.length ? `<div class="speed-reference"><span>怪物基准移速 · 像素/秒</span><div>${references.map(({type, speed}) => `<span>${escapeHtml(monsterNames[type] || type)} <strong>${speed}</strong></span>`).join("")}</div></div>` : '<p class="info-note">怪物基准移速暂不可用</p>';
  $("#inspector").innerHTML = `<div class="line"><h2 class="grow">出怪组 ${state.group + 1}</h2><button class="small-button" data-action="move-group-up" title="出怪组上移" aria-label="出怪组上移">↑</button><button class="small-button" data-action="move-group-down" title="出怪组下移" aria-label="出怪组下移">↓</button><button class="small-button" data-action="copy-group" title="复制出怪组" aria-label="复制出怪组">⧉</button><button class="small-button danger-button" data-action="delete-group" title="删除出怪组" aria-label="删除出怪组">×</button></div>
    <div class="stack"><div class="field"><label>怪物构成</label><div class="segment"><button data-action="composition-single" class="${mixed ? "" : "active"}">同类</button><button data-action="composition-mixed" class="${mixed ? "active" : ""}">混合</button></div></div>
    ${mixed ? `<div class="field"><label>出场顺序</label><div class="chip-list">${group.types.map((type, index) => `<div class="token"><select data-action="type-at" data-index="${index}">${options(typeOptions, type, monsterNames)}</select><button data-action="remove-type" data-index="${index}" title="移除怪物" aria-label="移除怪物">×</button></div>`).join("")}</div><button class="secondary-button" data-action="add-type">+ 添加怪物</button></div>` : `<div class="field-grid">${field("group", "type", "怪物类型", options(typeOptions, group.type, monsterNames), "select")}${field("group", "count", "数量", group.count, "number")}</div>`}
    <div class="divider"></div><div class="field-grid">${field("group", "formation", "出怪形式", options(Object.keys(formationNames), group.formation || "scatter", formationNames), "select")}${field("group", "from", "进入方向", options(Object.keys(originNames), group.from || "spawn", originNames), "select")}${field("group", "lane", "起始车道 · -1 随机", group.lane ?? -1, "number")}${field("group", "delay", "延迟 · 秒", group.delay ?? 0, "number")}${field("group", "interval", "间隔 · 秒", group.interval ?? 1, "number")}</div>
    <div class="divider"></div>${speedReference}<div class="field"><label class="line"><input data-scope="group" data-field="useConfiguredSpeed" type="checkbox" ${group.useConfiguredSpeed ? "checked" : ""}> 使用指定移速</label></div>${group.useConfiguredSpeed ? field("group", "configuredSpeed", "整组移速 · 像素/秒", group.configuredSpeed ?? "", "number") : ""}
    </div>`;
}
function nodesForTrack() {
  const data = documentData();
  return state.track === "spells" ? data.skills : data.upgrades.filter((node) => trackOf(node) === state.track);
}
function renderTree() {
  const nodes = nodesForTrack();
  const selected = currentNode() || nodes[0];
  if (selected) state.node = selected.id;
  $("#main-content").innerHTML = `<div class="page-head"><div><p class="eyebrow">PROGRESSION / ${state.track.toUpperCase()}</p><h1>${trackNames[state.track]}</h1><div class="meta-row"><span><strong>${nodes.length}</strong> 节点</span></div></div></div><h2 class="section-title">节点关系</h2>${renderGraph(nodes)}<h2 class="section-title">成长数值</h2>${state.track === "spells" ? `<p class="info-note">法术按关卡里程碑或水晶购买解锁。</p>` : `<p class="info-note">升级等级由价格与效果数组共同决定；每个价格对应下一等级。</p>`}`;
  renderNodeInspector(selected);
}
function renderBalance() {
  if (state.balanceCategory === "monsters") renderMonsters();
  else renderBaseUpgrades();
}
function renderBaseUpgrades() {
  const upgrades = documentData().upgrades;
  const ordered = ["bow", "magic", "defense"].flatMap((track) => upgrades.filter((entry) => trackOf(entry) === track));
  const selected = currentBaseUpgrade();
  $("#main-content").innerHTML = `<div class="page-head"><div><p class="eyebrow">BASE VALUES / PROGRESSION</p><h1>成长初始值</h1><div class="meta-row"><span><strong>${upgrades.length}</strong> 属性</span></div></div></div>
    <div class="monster-table-wrap"><table class="monster-table"><thead><tr><th>属性</th><th>所属</th><th>初始值</th><th>首次升级</th></tr></thead><tbody>${ordered.map((entry) => `<tr class="${entry.id === state.baseUpgrade ? "selected" : ""}"><th scope="row"><button data-action="select-base-upgrade" data-id="${escapeHtml(entry.id)}">${escapeHtml(entry.name)}<small>${escapeHtml(entry.id)}</small></button></th><td>${trackNames[trackOf(entry)]}</td><td>${escapeHtml(entry.values[0])}</td><td>${escapeHtml(entry.values[1])}</td></tr>`).join("")}</tbody></table></div>`;
  $("#inspector").innerHTML = `<p class="eyebrow">BASE / ${escapeHtml(selected.id)}</p><h2>${escapeHtml(selected.name)}</h2><div class="stack"><p class="info-note">${escapeHtml(selected.description)}</p>${field("base-upgrade", "baseValue", "初始值", selected.values[0], "number")}<div class="speed-reference"><span>首次升级后</span><div><strong>${escapeHtml(selected.values[1])}</strong></div></div></div>`;
}
function renderMonsters() {
  const data = documentData();
  const speeds = monsterSpeeds();
  const entries = Object.entries(monsterNames);
  $("#main-content").innerHTML = `<div class="page-head"><div><p class="eyebrow">BASE VALUES / MONSTERS</p><h1>怪物基础数值</h1><div class="meta-row"><span><strong>${entries.length}</strong> 类型</span><span>关卡血量按基础生命随关卡增长</span></div></div></div>
    <h2 class="section-title">全局基准</h2><div class="field-grid monster-global">${monsterField("monster-global", "baseSpeed", "基础移速 · 像素/秒", data.baseSpeed, 1, 500)}${monsterField("monster-global", "baseBodyRadius", "基础体型 · 半径像素", data.baseBodyRadius, 4, 48)}</div>
    <div class="monster-mobile-portrait"><strong>${monsterNames[state.monster]}</strong><canvas data-monster-art="${state.monster}" data-detail="true" width="256" height="180" role="img" aria-label="${monsterNames[state.monster]}怪物外形"></canvas></div>
    <h2 class="section-title">类型对照</h2><div class="monster-table-wrap"><table class="monster-table"><thead><tr><th>怪物</th><th>生命</th><th>移速</th><th>攻击</th><th>间隔</th><th>金币</th></tr></thead><tbody>${entries.map(([id, name]) => { const value = data.monsters[id]; return `<tr class="${id === state.monster ? "selected" : ""}" data-monster-row="${id}"><th scope="row"><button class="monster-name" data-action="select-monster" data-id="${id}"><canvas class="monster-thumb" data-monster-art="${id}" width="48" height="48" aria-hidden="true"></canvas><span>${name}<small>${id}</small></span></button></th><td>${value.maxHp}</td><td>${speeds[id]}</td><td>${value.actionPower}</td><td>${value.actionInterval}s</td><td>${value.coinDrop}</td></tr>`; }).join("")}</tbody></table></div>`;
  const monster = data.monsters[state.monster];
  $("#inspector").innerHTML = `<p class="eyebrow">MONSTER / ${escapeHtml(state.monster)}</p><h2>${monsterNames[state.monster]}</h2><div class="monster-portrait-stage"><canvas data-monster-art="${state.monster}" data-detail="true" width="256" height="180" role="img" aria-label="${monsterNames[state.monster]}怪物外形"></canvas></div><div class="stack">
    <h3>战斗与掉落</h3><div class="field-grid">${monsterField("monster", "maxHp", "基础生命", monster.maxHp, 1, 999)}${monsterField("monster", "coinDrop", "击杀金币", monster.coinDrop, 1, 100, 1)}${monsterField("monster", "actionPower", "行动强度", monster.actionPower, 0, 999, 1)}${monsterField("monster", "actionInterval", "行动间隔 · 秒", monster.actionInterval, 0.05, 60)}</div>
    <div class="divider"></div><h3>移动与体型</h3><div class="field-grid">${monsterField("monster", "speedMultiplier", "移速倍率", monster.speedMultiplier, 0.1, 10)}${monsterField("monster", "bodyScale", "体型倍率", monster.bodyScale, 0.25, 5)}</div><div class="speed-reference"><span>当前基准移速</span><div><strong>${speeds[state.monster]} 像素/秒</strong></div></div>
    <div class="divider"></div><h3>行为</h3><div class="field-grid">${monsterField("monster", "stopRatioMin", "驻足位置下限", monster.stopRatioMin, 0, 0.95)}${monsterField("monster", "stopRatioMax", "驻足位置上限", monster.stopRatioMax, 0, 0.95)}${monsterField("monster", "knockbackFactor", "受击退倍率", monster.knockbackFactor, 0, 2)}</div>
    </div>`;
  document.querySelectorAll("[data-monster-art]").forEach((canvas) => {
    const attributes = data.monsters[canvas.dataset.monsterArt];
    MonsterVisual.draw(canvas, canvas.dataset.monsterArt, data.baseBodyRadius,
      attributes.bodyScale, canvas.dataset.detail === "true");
  });
}
function renderGraph(nodes) {
  if (!nodes.length) return '<div class="tree-graph"><p class="empty">暂无节点</p></div>';
  const defense = state.track === "defense";
  const positions = new Map(nodes.map((node, index) => [node.id, {
    x: defense ? (index + 1) / (nodes.length + 1) : node.treeX,
    y: defense ? 110 : 75 + node.treeRow * 110,
  }]));
  const height = Math.max(350, ...[...positions.values()].map((position) => position.y + 120));
  const links = nodes.filter((node) => node.requires && positions.has(node.requires)).map((node) => {
    const from = positions.get(node.requires), to = positions.get(node.id);
    const start = from.y + 25, end = to.y - 25, turn = (start + end) / 2;
    return `<path d="M ${from.x * 1000} ${start} V ${turn} H ${to.x * 1000} V ${end}"/>`;
  }).join("");
  return `<div class="tree-graph" style="height:${height}px"><svg viewBox="0 0 1000 ${height}" preserveAspectRatio="none">${links}</svg>${nodes.map((node) => {
    const position = positions.get(node.id);
    return `<button class="tree-node ${node.id === state.node ? "selected" : ""} ${position.x > .55 ? "right-label" : ""}" style="left:${position.x * 100}%;top:${position.y}px" data-action="select-node" data-id="${escapeHtml(node.id)}" title="${escapeHtml(node.name)}">${escapeHtml(node.name.slice(0, 1))}<span>${escapeHtml(node.name)}</span></button>`;
  }).join("")}</div>`;
}
function renderNodeInspector(node) {
  if (!node) { $("#inspector").innerHTML = ""; return; }
  const isSkill = state.track === "spells";
  let html = `<p class="eyebrow">${isSkill ? "SPELL" : "UPGRADE"} / ${escapeHtml(node.id)}</p><h2>${escapeHtml(node.name)}</h2><div class="stack">${field("node", "name", "名称", node.name)}${field("node", "description", "效果说明", node.description, "textarea")}`;
  if (isSkill) {
    html += `<div class="field-grid">${field("node", "unlockStage", "解锁关卡 · 0 不启用", node.unlockStage, "number")}${field("node", "unlockCost", "水晶价格 · 0 不启用", node.unlockCost, "number")}</div><div class="field"><label class="line"><input data-scope="node" data-field="defaultUnlocked" type="checkbox" ${node.defaultUnlocked ? "checked" : ""}> 默认解锁</label></div>`;
    html += field("node", "requires", "前置法术", options(["", ...documentData().skills.filter((item) => item.id !== node.id).map((item) => item.id)], node.requires, {"": "无"}), "select");
  } else {
    html += field("node", "currency", "货币", options(["coins", "crystals"], node.currency, {coins: "金币", crystals: "水晶"}), "select");
    if (node.track) {
      const parents = documentData().upgrades.filter((item) => item.track === node.track && item.id !== node.id);
      html += field("node", "requires", "前置升级", options(["", ...parents.map((item) => item.id)], node.requires, {"": "无"}), "select");
      html += field("node", "requiredLevel", "前置等级 · 0 无前置", node.requiredLevel ?? 0, "number");
    }
  }
  if (isSkill || node.track) {
    html += `<div class="field-grid">${field("node", "treeRow", "树层级 · 从 0 开始", node.treeRow, "number")}${field("node", "treeX", "横向位置 · 0–1", node.treeX, "number")}</div><div class="field"><input data-scope="node" data-field="treeX" type="range" min="0.12" max="0.88" step="0.01" value="${escapeHtml(node.treeX)}" aria-label="横向位置"></div>`;
  }
  if (!isSkill) {
    html += `<div class="divider"></div><div class="line"><h3 class="grow">等级数值</h3><button class="small-button" data-action="add-upgrade-level" title="增加一级" aria-label="增加一级">+</button><button class="small-button danger-button" data-action="remove-upgrade-level" title="移除末级" aria-label="移除末级" ${node.costs.length <= 1 ? "disabled" : ""}>×</button></div>`;
    html += node.values.map((value, index) => `<div class="level-row"><span>${index === 0 ? "初始" : `${index} 级`}</span><input type="number" step="any" value="${escapeHtml(value)}" data-action="edit-array" data-array="values" data-index="${index}" aria-label="${index} 级效果"><input type="number" step="1" value="${index < node.costs.length ? escapeHtml(node.costs[index]) : ""}" data-action="edit-array" data-array="costs" data-index="${index}" aria-label="升级价格" ${index >= node.costs.length ? "disabled" : ""}><span>${index < node.costs.length ? "花费" : ""}</span></div>`).join("");
  }
  $("#inspector").innerHTML = html + "</div>";
}
function openPreview() {
  previewState.open = true;
  previewState.wave = state.wave;
  previewState.time = 0;
  previewState.playing = true;
  previewState.lastFrame = 0;
  document.body.classList.add("preview-open");
  $(".app-shell").inert = true;
  $("#level-preview").hidden = false;
  rebuildPreviewTimeline();
  renderPreview();
  $("#level-preview [data-action=close-preview]").focus();
  previewState.frame = requestAnimationFrame(previewTick);
}
function closePreview() {
  previewState.open = false;
  previewState.playing = false;
  cancelAnimationFrame(previewState.frame);
  $("#level-preview").hidden = true;
  $("#level-preview").innerHTML = "";
  document.body.classList.remove("preview-open");
  $(".app-shell").inert = false;
  $("[data-action=open-preview]")?.focus();
}
function rebuildPreviewTimeline() {
  const data = state.documents.monsters.data;
  previewState.timeline = PreviewModel.buildWave(currentLevel().waves[previewState.wave], {
    randomize: previewState.randomize,
    baseSpeed: data.baseSpeed,
    monsterSpeeds: monsterSpeeds(),
  });
}
function renderPreview() {
  if (!previewState.open) return;
  const level = currentLevel();
  const wave = level.waves[previewState.wave];
  const timeline = previewState.timeline;
  const groups = wave.groups;
  const count = timeline.events.length;
  $("#level-preview").innerHTML = `<div class="preview-panel">
    <header class="preview-header"><div><p class="eyebrow">CAMPAIGN / ${String(state.level + 1).padStart(2, "0")}</p><h2>${escapeHtml(level.name)}</h2><p class="preview-subtitle">出怪预览 · ${level.waves.length} 波 · 本波 ${count} 只</p></div><button class="preview-close" data-action="close-preview" type="button" title="关闭预览" aria-label="关闭预览">×</button></header>
    <div class="preview-body"><section class="preview-battle"><div class="preview-battle-head"><strong>${escapeHtml(wave.name)}</strong><span id="preview-count">0 / ${count}</span></div>
      <div class="preview-stage" aria-label="12 车道战场示意图"><div class="preview-ground"></div>${Array.from({length: 12}, (_, lane) => `<div class="preview-lane" style="left:${(lane + .5) / 12 * 100}%"><span>${lane + 1}</span></div>`).join("")}
      <div class="preview-spawn-line"></div><div class="preview-wall"><div class="preview-merlons"></div><div class="preview-gate"></div></div>
      ${timeline.events.map((event, index) => `<div class="preview-monster type-${monsterNames[event.type] ? escapeHtml(event.type) : "other"}" data-event-index="${index}" title="${escapeHtml(monsterNames[event.type] || event.type)} · 第 ${event.groupIndex + 1} 组 · ${event.time.toFixed(1)} 秒 · ${event.lane + 1} 车道 · ${event.configuredSpeed > 0 ? `${event.configuredSpeed} px/s` : event.baselineSpeed > 0 ? `${(event.baselineSpeed * event.speedFactor).toFixed(1)} px/s` : `${event.speedFactor.toFixed(2)} 倍个体速度`}" hidden>${escapeHtml((monsterNames[event.type] || event.type).slice(0, 1))}</div>`).join("")}</div>
      <div class="preview-controls"><button class="preview-control" data-action="preview-restart" type="button" title="重新播放" aria-label="重新播放">↺</button><button id="preview-play-button" class="preview-control play" data-action="preview-play" type="button" title="暂停" aria-label="暂停">Ⅱ</button><span id="preview-clock" class="preview-clock">0.0s</span><input id="preview-scrub" type="range" min="0" max="${timeline.duration}" step="0.05" value="${previewState.time}" aria-label="预览时间"><span class="preview-clock">${timeline.duration.toFixed(1)}s</span></div>
    </section><aside class="preview-detail"><div class="preview-detail-head"><span>预览设置</span></div><div class="preview-options"><label class="preview-random"><input id="preview-random" type="checkbox" ${previewState.randomize ? "checked" : ""}> 模拟随机</label><div class="preview-speed" aria-label="播放速度">${[1, 2, 4].map((speed) => `<button data-action="preview-speed" data-speed="${speed}" class="${previewState.speed === speed ? "selected" : ""}" type="button">${speed}×</button>`).join("")}</div></div>
      <div class="preview-detail-head"><span>波次</span><span>${level.waves.length} 波</span></div>
      <div class="preview-wave-list">${level.waves.map((item, index) => `<button data-action="preview-wave" data-index="${index}" class="preview-wave ${index === previewState.wave ? "selected" : ""}" type="button"><span>${String(index + 1).padStart(2, "0")} · ${escapeHtml(item.name)}</span><small>${item.groups.reduce((sum, group) => sum + (group.types?.length || group.count || 0), 0)}</small></button>`).join("")}</div>
      <div class="preview-detail-head group-head"><span>出怪组</span><span>已出 / 总数</span></div><div class="preview-group-list">${groups.map((group, index) => `<div class="preview-group"><div><strong>${String(index + 1).padStart(2, "0")} · ${escapeHtml((group.types || [group.type]).map((type) => monsterNames[type] || type).join(" / "))}</strong><span data-group-index="${index}">0 / ${group.types?.length || group.count || 0}</span></div><small>${formationNames[group.formation || "scatter"]} · 延迟 ${group.delay || 0}s · ${groupSpeedLabel(group)}</small></div>`).join("")}</div>
      <p class="preview-caveat" title="下一波会在场上怪物清零且休整结束后出现；预览使用怪物属性表的默认移速，战斗中的停步和游荡不在此模拟。">${previewState.randomize ? "随机抽样" : "固定示意"} · 波次衔接取决于实战清场</p>
    </aside></div></div>`;
  updatePreviewFrame();
}
function updatePreviewFrame() {
  if (!previewState.open || !previewState.timeline) return;
  const {events, duration} = previewState.timeline;
  const time = Math.min(previewState.time, duration);
  $("#preview-clock").textContent = `${time.toFixed(1)}s`;
  $("#preview-scrub").value = time;
  const button = $("#preview-play-button");
  button.textContent = previewState.playing ? "Ⅱ" : "▶";
  button.title = button.ariaLabel = previewState.playing ? "暂停" : "播放";
  let spawned = 0;
  const perGroup = new Array(currentLevel().waves[previewState.wave].groups.length).fill(0);
  $("#level-preview").querySelectorAll(".preview-monster").forEach((marker) => {
    const event = events[Number(marker.dataset.eventIndex)];
    const age = time - event.time;
    const travel = PreviewModel.travelDuration(event);
    marker.hidden = age < 0 || age > travel;
    if (!marker.hidden) {
      const progress = Math.max(0, age / travel);
      const {startX, startY, targetX, targetY} = PreviewModel.position(event);
      marker.style.left = `${(startX + (targetX - startX) * progress) * 100}%`;
      marker.style.top = `${(startY + (targetY - startY) * progress) * 100}%`;
    }
    if (age >= 0) { spawned++; perGroup[event.groupIndex]++; }
  });
  $("#preview-count").textContent = `${spawned} / ${events.length}`;
  $("#level-preview").querySelectorAll("[data-group-index]").forEach((element) => {
    const index = Number(element.dataset.groupIndex);
    const group = currentLevel().waves[previewState.wave].groups[index];
    element.textContent = `${perGroup[index]} / ${group.types?.length || group.count || 0}`;
  });
}
function previewTick(timestamp) {
  if (!previewState.open || !previewState.playing) return;
  if (previewState.lastFrame) previewState.time += Math.min(.1, (timestamp - previewState.lastFrame) / 1000) * previewState.speed;
  previewState.lastFrame = timestamp;
  if (previewState.time >= previewState.timeline.duration) {
    previewState.time = previewState.timeline.duration;
    previewState.playing = false;
  }
  updatePreviewFrame();
  if (previewState.playing) previewState.frame = requestAnimationFrame(previewTick);
}
function setCurrentField(input) {
  const scope = input.dataset.scope;
  const target = scope === "level" ? currentLevel() : scope === "wave" ? currentWave() :
    scope === "group" ? currentGroup() : scope === "monster-global" ? documentData() :
      scope === "monster" ? documentData().monsters[state.monster] :
        scope === "base-upgrade" ? currentBaseUpgrade() : currentNode();
  if (!target) return;
  let value = input.type === "checkbox" ? input.checked : input.type === "number" || input.type === "range" ? Number(input.value) : input.value;
  if (input.type === "number" && input.value.trim() === "") value = NaN;
  if (scope === "base-upgrade") target.values[0] = value;
  else target[input.dataset.field] = value;
  if (scope === "group" && input.dataset.field === "useConfiguredSpeed" && value && !positive(target.configuredSpeed)) {
    const references = speedReferences(target);
    if (references.length === 1) target.configuredSpeed = references[0].speed;
    else delete target.configuredSpeed;
  }
  if (scope === "group" && input.dataset.field === "formation" && value !== "scatter")
    target.from = "spawn";
  markDirty();
  render();
}
function swap(array, first, second) {
  if (second < 0 || second >= array.length) return false;
  [array[first], array[second]] = [array[second], array[first]];
  return true;
}
function handleAction(button) {
  const action = button.dataset.action;
  const index = Number(button.dataset.index);
  if (action === "open-preview") { openPreview(); return; }
  if (action === "close-preview") { closePreview(); return; }
  if (action === "preview-play" || action === "preview-restart") {
    cancelAnimationFrame(previewState.frame);
    const replay = action === "preview-restart" || (!previewState.playing && previewState.time >= previewState.timeline.duration);
    if (replay) {
      previewState.time = 0;
      if (previewState.randomize) { rebuildPreviewTimeline(); renderPreview(); }
    }
    previewState.playing = action === "preview-restart" || !previewState.playing;
    previewState.lastFrame = 0;
    updatePreviewFrame();
    if (previewState.playing) previewState.frame = requestAnimationFrame(previewTick);
    return;
  }
  if (action === "preview-wave") {
    cancelAnimationFrame(previewState.frame);
    previewState.wave = index; previewState.time = 0; previewState.playing = false;
    rebuildPreviewTimeline();
    renderPreview();
    return;
  }
  if (action === "preview-speed") { previewState.speed = Number(button.dataset.speed); renderPreview(); return; }
  if (action === "select-level") { state.level = index; state.wave = 0; state.group = 0; }
  else if (action === "select-wave") { state.wave = index; state.group = 0; }
  else if (action === "select-group") state.group = index;
  else if (action === "select-track") { state.track = button.dataset.track; state.node = nodesForTrack()[0]?.id; }
  else if (action === "select-node") state.node = button.dataset.id;
  else if (action === "select-balance-category") state.balanceCategory = button.dataset.category;
  else if (action === "select-base-upgrade") state.baseUpgrade = button.dataset.id;
  else if (action === "select-monster") state.monster = button.dataset.id;
  else if (action === "add-level") {
    const copy = clone(currentLevel()); copy.name = "新关卡";
    state.documents.levels.data.levels.splice(++state.level, 0, copy);
    state.wave = 0; state.group = 0; markDirty();
  } else if (action === "delete-level") {
    if (state.documents.levels.data.levels.length <= 1 || !confirm("删除当前关卡？")) return;
    state.documents.levels.data.levels.splice(state.level, 1);
    state.level = Math.min(state.level, state.documents.levels.data.levels.length - 1);
    state.wave = 0; state.group = 0; markDirty();
  } else if (action === "add-wave" || action === "copy-wave") {
    const copy = action === "copy-wave" ? clone(currentWave()) : {name: "新波次", restAfter: 3, groups: [{type: "normal", count: 3, interval: 1, delay: 0, from: "spawn", formation: "scatter"}]};
    currentLevel().waves.splice(++state.wave, 0, copy);
    state.group = 0; markDirty();
  } else if (action === "delete-wave") {
    if (currentLevel().waves.length <= 1 || !confirm("删除当前波次？")) return;
    currentLevel().waves.splice(state.wave, 1); state.wave = Math.min(state.wave, currentLevel().waves.length - 1);
    state.group = 0; markDirty();
  } else if (action === "move-wave-up" || action === "move-wave-down") {
    const next = state.wave + (action.endsWith("up") ? -1 : 1);
    if (swap(currentLevel().waves, state.wave, next)) { state.wave = next; markDirty(); }
  } else if (action === "add-group" || action === "copy-group") {
    const copy = action === "copy-group" ? clone(currentGroup()) : {type: "normal", count: 3, interval: 1, delay: 0, from: "spawn", formation: "scatter"};
    currentWave().groups.splice(++state.group, 0, copy); markDirty();
  } else if (action === "delete-group") {
    if (currentWave().groups.length <= 1 || !confirm("删除当前出怪组？")) return;
    currentWave().groups.splice(state.group, 1); state.group = Math.min(state.group, currentWave().groups.length - 1); markDirty();
  } else if (action === "move-group-up" || action === "move-group-down") {
    const next = state.group + (action.endsWith("up") ? -1 : 1);
    if (swap(currentWave().groups, state.group, next)) { state.group = next; markDirty(); }
  } else if (action === "composition-single" && currentGroup().types) {
    const group = currentGroup(); group.type = group.types[0] || "normal"; group.count = group.types.length;
    delete group.types; markDirty();
  } else if (action === "composition-mixed" && !currentGroup().types) {
    const group = currentGroup(); group.types = Array.from({length: group.count}, () => group.type);
    delete group.type; delete group.count; markDirty();
  } else if (action === "add-type") { currentGroup().types.push(monsterTypes()[0] || "normal"); markDirty(); }
  else if (action === "remove-type") {
    if (currentGroup().types.length <= 1) return;
    currentGroup().types.splice(index, 1); markDirty();
  } else if (action === "add-upgrade-level") {
    const node = currentNode(); node.costs.push(Math.max(1, (node.costs.at(-1) || 1) * 2));
    node.values.push(node.values.at(-1)); markDirty();
  } else if (action === "remove-upgrade-level") {
    const node = currentNode(); if (node.costs.length <= 1) return;
    node.costs.pop(); node.values.pop(); markDirty();
  } else return;
  render();
  if (action === "select-monster" && matchMedia("(max-width: 850px)").matches)
    $(".monster-mobile-portrait").scrollIntoView({behavior: "smooth", block: "start"});
}

document.addEventListener("click", (event) => {
  if (event.target.id === "level-preview") { closePreview(); return; }
  const button = event.target.closest("[data-action]");
  if (button && button.tagName === "BUTTON") handleAction(button);
});
document.addEventListener("change", (event) => {
  const input = event.target;
  if (input.id === "preview-random") {
    cancelAnimationFrame(previewState.frame);
    previewState.randomize = input.checked;
    previewState.time = 0; previewState.playing = false; previewState.lastFrame = 0;
    rebuildPreviewTimeline(); renderPreview();
    $("#preview-random").focus();
    return;
  }
  if (input.id === "level-search") { state.search = input.value; renderSidebar(); return; }
  if (input.dataset.scope) { setCurrentField(input); return; }
  if (input.dataset.action === "type-at") { currentGroup().types[Number(input.dataset.index)] = input.value; markDirty(); render(); return; }
  if (input.dataset.action === "edit-array") {
    currentNode()[input.dataset.array][Number(input.dataset.index)] = Number(input.value);
    markDirty(); render();
  }
});
document.addEventListener("input", (event) => {
  if (event.target.id === "level-search") { state.search = event.target.value; renderSidebar(); $("#level-search").focus(); }
  if (event.target.id === "preview-scrub") { previewState.time = Number(event.target.value); previewState.lastFrame = 0; updatePreviewFrame(); }
});
document.addEventListener("keydown", (event) => {
  if (event.key === "Escape" && previewState.open) closePreview();
});
$("#mode-levels").addEventListener("click", () => { state.mode = "levels"; render(); });
$("#mode-tree").addEventListener("click", () => { state.mode = "tree"; render(); });
$("#mode-balance").addEventListener("click", () => { state.mode = "balance"; render(); });
$("#save-button").addEventListener("click", saveCurrent);
$("#reload-button").addEventListener("click", () => {
  if (Object.values(state.dirty).some(Boolean) && !confirm("放弃未保存的修改并重新载入？")) return;
  loadAll().catch((error) => showToast(error.message, true));
});
window.addEventListener("beforeunload", (event) => {
  if (Object.values(state.dirty).some(Boolean)) event.preventDefault();
});
loadAll().catch((error) => showToast(error.message, true));
