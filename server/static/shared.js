(function () {
  function getCookie(name) {
    try {
      const all = "; " + document.cookie;
      const parts = all.split("; " + String(name || "") + "=");
      if (parts.length < 2) return "";
      return decodeURIComponent(parts.pop().split(";").shift() || "");
    } catch {
      return "";
    }
  }

  const originalFetch = window.fetch ? window.fetch.bind(window) : null;
  if (originalFetch) {
    window.fetch = function patchedFetch(input, init) {
      const nextInit = init ? Object.assign({}, init) : {};
      const method = String((nextInit.method || "GET")).toUpperCase();
      if (["POST", "PUT", "PATCH", "DELETE"].includes(method)) {
        const headers = new Headers(nextInit.headers || {});
        if (!headers.has("X-CSRF-Token")) {
          const token = getCookie("app_lvgl_csrf");
          if (token) headers.set("X-CSRF-Token", token);
        }
        nextInit.headers = headers;
      }
      return originalFetch(input, nextInit);
    };
  }

  function el(tag, attrs, children) {
    const n = document.createElement(tag);
    if (attrs) {
      for (const [k, v] of Object.entries(attrs)) {
        if (k === "class") n.className = v;
        else if (k === "text") n.textContent = v;
        else if (k.startsWith("on") && typeof v === "function") n.addEventListener(k.slice(2), v);
        else n.setAttribute(k, String(v));
      }
    }
    (children || []).forEach((c) => n.appendChild(c));
    return n;
  }

  function safePath(p) {
    try { return String(p || "/"); } catch { return "/"; }
  }

  function navItems() {
    // 已迁移页面优先走 /console/*，未迁移的兼容页保留原入口
    return [
      { href: "/console/", label: "主页" },
      { href: "/console/can", label: "CAN 监控" },
      { href: "/hardware", label: "硬件监控" },
      { href: "/console/dbc", label: "DBC 解析" },
      { href: "/console/uds", label: "UDS" },
      { href: "/files", label: "文件管理" },
      { href: "/console/device-config-v2", label: "设备配置" },
    ];
  }

  function matchActive(href, cur) {
    const h = safePath(href);
    const c = safePath(cur);
    if (h === "/") return c === "/";
    return c === h || c.startsWith(h + "/");
  }

  function insertHeader() {
    if (document.documentElement.dataset.noSharedHeader === "1") return;
    if (document.querySelector(".app-header")) return;

    const title = (document.title || "Tina Web").trim();
    const cur = safePath(location.pathname);
    const hideStatus = document.documentElement.dataset.hideSharedStatus === "1";
    const hideRefresh = document.documentElement.dataset.hideSharedRefresh === "1";

    const brand = el("div", { class: "app-brand" }, [
      el("div", { class: "app-brand__title", text: title }),
    ]);

    const nav = el("nav", { class: "app-nav" }, navItems().map((it) => {
      const a = el("a", { href: it.href, text: it.label });
      if (matchActive(it.href, cur)) a.classList.add("active");
      return a;
    }));

    let status = null;
    let btn = null;
    if (!hideStatus) {
      const serverDot = el("span", { class: "app-dot warn", id: "appDotServer" });
      const deviceDot = el("span", { class: "app-dot warn", id: "appDotDevice" });
      const serverText = el("span", { id: "appTxtServer", text: "服务器：检查中" });
      const deviceText = el("span", { id: "appTxtDevice", text: "设备：检查中" });
      const buildText = el("span", { id: "appTxtBuild", text: "版本：检查中" });
      const buildPill = el("span", {
        class: "app-pill app-pill--debug",
        id: "appPillBuild",
        title: "部署版本检查中",
      }, [buildText]);
      if (!hideRefresh) {
        btn = el("button", { class: "app-btn secondary", id: "appBtnRefresh", text: "刷新" });
      }

      const children = [
        el("span", { class: "app-pill" }, [serverDot, serverText]),
        el("span", { class: "app-pill" }, [deviceDot, deviceText]),
        buildPill,
      ];
      if (btn) children.push(btn);
      status = el("div", { class: "app-status" }, children);
    }

    const innerChildren = status ? [brand, nav, status] : [brand, nav];
    const header = el("header", { class: "app-header" }, [
      el("div", { class: "app-header__inner" }, innerChildren),
    ]);

    document.body.prepend(header);

    // 大多数页面需要预留顶部空间；全屏类页面不强制 padding
    const hasFullscreenStage = !!document.querySelector(".stage, canvas#bg");
    document.documentElement.classList.add("app-has-header");
    if (hasFullscreenStage) {
      // 覆盖 padding（避免影响全屏画布布局）
      document.documentElement.classList.remove("app-has-header");
    }

    if (!hideStatus) {
      if (btn) btn.addEventListener("click", () => refreshStatus(true));
      refreshStatus(false);
      setInterval(() => refreshStatus(false), 5000);
    }
  }

  async function fetchWithTimeout(url, ms) {
    const ctrl = new AbortController();
    const t = setTimeout(() => ctrl.abort(), ms);
    try {
      const r = await fetch(url, { signal: ctrl.signal, cache: "no-store" });
      return r;
    } finally {
      clearTimeout(t);
    }
  }

  let inFlight = false;
  async function refreshStatus(force) {
    if (inFlight && !force) return;
    inFlight = true;
    try {
      const dotS = document.getElementById("appDotServer");
      const dotD = document.getElementById("appDotDevice");
      const txtS = document.getElementById("appTxtServer");
      const txtD = document.getElementById("appTxtDevice");
      const txtB = document.getElementById("appTxtBuild");
      const pillB = document.getElementById("appPillBuild");
      if (!dotS || !dotD || !txtS || !txtD) return;

      let okServer = false;
      let okDevice = false;
      let deviceAddr = "";
      let deviceId = "";
      let serverInfo = {};

      try {
        // 使用 fast 状态接口：不做 ping，不阻塞，刷新页面时不会卡 UI
        const r = await fetchWithTimeout("/api/status_fast", 800);
        okServer = r.ok;
        const j = okServer ? await r.json().catch(() => ({})) : {};
        okDevice = !!(j && j.hub && j.hub.connected);
        deviceAddr = (j && j.hub && j.hub.client_addr) ? String(j.hub.client_addr) : "";
        deviceId = (j && j.hub && j.hub.client_id) ? String(j.hub.client_id) : "";
        serverInfo = (j && j.server) ? j.server : {};
      } catch (e) {
        okServer = false;
      }

      dotS.className = "app-dot " + (okServer ? "good" : "bad");
      txtS.textContent = okServer ? "服务器：在线" : "服务器：离线";

      dotD.className = "app-dot " + (okDevice ? "good" : "warn");
      if (okDevice) {
        const suffix = deviceAddr ? ` (${deviceAddr})` : (deviceId ? ` (${deviceId})` : "");
        txtD.textContent = "设备：已连接" + suffix;
      } else {
        txtD.textContent = "设备：未连接";
      }

      if (txtB) {
        const buildTag = String((serverInfo && serverInfo.build_tag) || "").trim();
        const gitCommit = String((serverInfo && serverInfo.git_commit) || "").trim();
        const startedAt = String((serverInfo && serverInfo.process_started_at) || "").trim();
        const hostname = String((serverInfo && serverInfo.hostname) || "").trim();
        const pid = String((serverInfo && serverInfo.pid) || "").trim();
        const shortLabel = [buildTag, gitCommit].filter(Boolean).join(" / ") || "unknown";
        txtB.textContent = "版本：" + shortLabel;
        if (pillB) {
          const tips = [];
          if (buildTag) tips.push("Build: " + buildTag);
          if (gitCommit) tips.push("Commit: " + gitCommit);
          if (startedAt) tips.push("Started: " + startedAt);
          if (hostname) tips.push("Host: " + hostname);
          if (pid) tips.push("PID: " + pid);
          pillB.title = tips.join("\n") || "部署版本未知";
        }
      }
    } finally {
      inFlight = false;
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", insertHeader);
  } else {
    insertHeader();
  }
})();
