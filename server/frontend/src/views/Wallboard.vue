<template>
  <div class="wallboard-empty">
    <div class="empty-shell">
      <p class="eyebrow">运行大屏</p>
      <h1>当前大屏内容已清空</h1>
      <p class="desc">该页面已保留为空白基础界面，后续可按新的展示需求重新配置模块、布局和数据内容。</p>
    </div>
  </div>
</template>

<style scoped>
.wallboard-empty {
  min-height: 100vh;
  padding: 40px;
  display: grid;
  place-items: center;
  background:
    radial-gradient(circle at top, rgba(208, 165, 103, 0.12), transparent 30%),
    linear-gradient(180deg, #0a111d, #070c15);
}

.empty-shell {
  width: min(960px, 100%);
  min-height: 420px;
  padding: 40px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  border-radius: 32px;
  border: 1px dashed rgba(136, 176, 255, 0.22);
  background: rgba(8, 18, 31, 0.72);
  box-shadow: 0 24px 80px rgba(0, 0, 0, 0.36);
}

.eyebrow {
  margin: 0 0 12px;
  color: #b9a17b;
  font-size: 13px;
  letter-spacing: 0.2em;
  text-transform: uppercase;
}

h1 {
  margin: 0;
  color: #f5f8ff;
  font-size: clamp(36px, 5vw, 64px);
  line-height: 1.08;
}

.desc {
  max-width: 42rem;
  margin: 18px 0 0;
  color: #9cb0cf;
  font-size: 18px;
  line-height: 1.8;
}
</style>
