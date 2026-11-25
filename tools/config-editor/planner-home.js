// Presentation only: all counts and draft indicators come from the editor's loaded documents.
const PlannerHome = (() => {
  const icons = {
    levels: '<path d="M6 27h7l6-14h7l5 8h11M7 8v7m-3-4h6M37 30v9m-4-4h8"/><circle cx="8" cy="27" r="3"/><circle cx="42" cy="21" r="3"/>',
    tree: '<path d="M24 14v10M9 32v-8h30v8"/><circle cx="24" cy="9" r="5"/><circle cx="9" cy="37" r="5"/><circle cx="24" cy="37" r="5"/><circle cx="39" cy="37" r="5"/><path d="M24 24v8"/>',
    balance: '<path d="M11 7v34M24 7v34M37 7v34"/><path d="M6 16h10v7H6zM19 29h10v7H19zM32 12h10v7H32z"/>',
  };
  function render(documents, dirty) {
    const levels = documents.levels.data.levels.length;
    const nodes = documents.tree.data.upgrades.length + documents.tree.data.skills.length;
    const monsters = Object.keys(documents.monsters.data.monsters).length;
    const card = (mode, index, title, copy, count, unit, tags, changed) => `<button class="tool-card tool-${mode}" data-action="open-tool" data-mode="${mode}">
      <span class="tool-card-top"><span class="tool-icon"><svg viewBox="0 0 48 48" aria-hidden="true">${icons[mode]}</svg></span><span class="tool-index">0${index}</span></span>
      <strong>${title}</strong><span class="tool-copy">${copy}</span><span class="tool-tags">${tags.map(tag => `<span>${tag}</span>`).join('')}</span>
      <span class="tool-card-bottom"><span>${changed ? '<i class="draft-dot"></i>有未保存修改' : `<b>${count}</b> ${unit}`}</span><span class="tool-open">进入工具 <span aria-hidden="true">↗</span></span></span>
    </button>`;
    const shortcut = (track, name, detail, glyph) => `<button class="planner-shortcut" data-action="open-tool" data-mode="tree" data-track="${track}"><span class="shortcut-glyph" aria-hidden="true">${glyph}</span><span><strong>${name}</strong><small>${detail}</small></span><span class="shortcut-arrow" aria-hidden="true">→</span></button>`;
    return `<section class="planner-home" aria-label="游戏策划工作台">
      <div class="planner-heading"><div><p class="eyebrow">CASTLEGUARD · 策划工作台</p><h1>让下一场战斗更精彩。</h1><p class="planner-intro">从关卡节奏到角色成长，在这里组织你的游戏设计。</p></div><span class="project-status"><i></i>本地项目已连接</span></div>
      <div class="planner-section-head"><h2>配置工具</h2><span>选择一项，开始设计</span></div>
      <div class="tool-grid">
        ${card('levels', 1, '关卡配置', '编排怪物与进攻队列，预览每一波战斗。', levels, '个关卡配置', ['出怪编排', '战场预览'], dirty.levels)}
        ${card('tree', 2, '技能树配置', '连接解锁条件，搭建武器、法术与城防的成长路线。', nodes, '个成长节点', ['多前置条件', '升级效果'], dirty.tree)}
        ${card('balance', 3, '基础数值', '统一调整怪物属性，让每一次成长都有合适的分量。', monsters, '种怪物类型', ['怪物外形', '成长初始值'], dirty.monsters || dirty.tree)}
      </div>
      <div class="planner-section-head shortcut-heading"><h2>成长体系快捷入口</h2><span>直接打开对应设施的技能树</span></div>
      <div class="shortcut-grid">
        ${shortcut('bow', '军械所', '弓箭属性与武器技能', '弓')}
        ${shortcut('magic', '研究院', '法力与法术强化', '法')}
        ${shortcut('defense', '城防营', '城墙与防御设施', '城')}
        ${shortcut('spells', '法术研习', '主动法术与解锁关系', '术')}
      </div>
      <div class="planner-footnote"><span class="planner-footnote-mark" aria-hidden="true">✓</span><p><strong>一份配置，贯通整个游戏。</strong><span>切换工具会保留本次修改。编辑完成后，在对应工具中点击“保存配置”。</span></p></div>
    </section>`;
  }
  return {render};
})();
if (typeof module !== 'undefined') module.exports = PlannerHome;
