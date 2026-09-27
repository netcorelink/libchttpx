(function () {
  "use strict";

  var REPO = "netcorelink/libchttpx";
  var API = "https://api.github.com/repos/" + REPO;

  window.CHTTPX_DOCS = [
    {
      title: "Getting started",
      items: [
        { href: "docs.html", label: "Overview", page: "docs" },
        { href: "docs.html#install", label: "Installation", page: "docs" },
        { href: "docs.html#first-server", label: "First server", page: "docs" },
        { href: "docs.html#compile", label: "Compile", page: "docs" }
      ]
    },
    {
      title: "Modules",
      items: [
        { href: "module.html?name=app", id: "app", label: "App runtime" },
        { href: "module.html?name=server", id: "server", label: "Server" },
        { href: "module.html?name=tls", id: "tls", label: "TLS / HTTPS" },
        { href: "module.html?name=compression", id: "compression", label: "Compression" },
        { href: "module.html?name=metrics", id: "metrics", label: "Metrics" },
        { href: "module.html?name=routing", id: "routing", label: "Routing" },
        { href: "module.html?name=middleware", id: "middleware", label: "Middleware" },
        { href: "module.html?name=request", id: "request", label: "Request" },
        { href: "module.html?name=responses", id: "responses", label: "Responses" },
        { href: "module.html?name=json", id: "json", label: "JSON" },
        { href: "module.html?name=memory", id: "memory", label: "Memory" },
        { href: "module.html?name=uploads", id: "uploads", label: "Uploads" },
        { href: "module.html?name=cors", id: "cors", label: "CORS" },
        { href: "module.html?name=cookies", id: "cookies", label: "Cookies" },
        { href: "module.html?name=i18n", id: "i18n", label: "Request IDs & i18n" },
        { href: "module.html?name=logging", id: "logging", label: "Logging" },
        { href: "module.html?name=rate-limiting", id: "rate-limiting", label: "Rate limiting" },
        { href: "module.html?name=websocket", id: "websocket", label: "WebSocket" }
      ]
    },
    {
      title: "More",
      items: [
        { href: "examples.html", label: "Examples", page: "examples" },
        { href: "blog.html", label: "Updates", page: "blog" },
        { href: "support.html", label: "Support", page: "support" },
        { href: "about.html", label: "About", page: "about" }
      ]
    }
  ];

  var currentPage = window.location.pathname.split("/").pop() || "index.html";
  var currentModule = new URLSearchParams(window.location.search).get("name") || "";

  var menuButton = document.querySelector("[data-menu-button]");
  var navLinks = document.querySelector("[data-nav-links]");
  var sidebar = document.querySelector("[data-docs-sidebar]");

  if (menuButton) {
    menuButton.addEventListener("click", function () {
      if (window.matchMedia("(max-width: 900px)").matches && sidebar) {
        var open = sidebar.classList.toggle("open");
        menuButton.setAttribute("aria-expanded", String(open));
        if (navLinks) navLinks.classList.remove("open");
        return;
      }
      if (navLinks) {
        var shown = navLinks.classList.toggle("open");
        menuButton.setAttribute("aria-expanded", String(shown));
      }
    });
  }

  document.querySelectorAll("[data-nav-link]").forEach(function (link) {
    var href = link.getAttribute("href");
    if (href === currentPage || (currentPage === "" && href === "index.html")) {
      link.classList.add("active");
    }
  });

  function linkActive(item) {
    if (item.id) return currentPage === "module.html" && item.id === currentModule;
    if (item.page === "docs") {
      if (currentPage !== "docs.html") return false;
      var hash = item.href.indexOf("#") >= 0 ? "#" + item.href.split("#")[1] : "";
      if (hash) return window.location.hash === hash;
      return !window.location.hash || window.location.hash === "#";
    }
    if (item.page) return currentPage === item.page + ".html";
    return item.href === currentPage;
  }

  function groupHasActive(group) {
    return group.items.some(function (item) {
      return linkActive(item);
    });
  }

  window.renderDocsSidebar = function (extraChildren) {
    var root = document.querySelector("[data-docs-sidebar]");
    if (!root) return;

    root.innerHTML = "";

    var search = document.createElement("input");
    search.type = "search";
    search.className = "sidebar-search";
    search.placeholder = "Search... (/)";
    search.setAttribute("aria-label", "Search documentation");
    search.setAttribute("data-sidebar-search", "");
    root.appendChild(search);

    window.CHTTPX_DOCS.forEach(function (group, index) {
      var block = document.createElement("div");
      block.className = "nav-group";
      if (groupHasActive(group) || index === 0) block.classList.add("open");

      var toggle = document.createElement("button");
      toggle.type = "button";
      toggle.className = "nav-group-toggle";
      toggle.setAttribute("aria-expanded", block.classList.contains("open") ? "true" : "false");

      var label = document.createElement("span");
      label.textContent = group.title;
      toggle.appendChild(label);

      var icon = document.createElement("span");
      icon.className = "nav-toggle-icon";
      icon.setAttribute("aria-hidden", "true");
      toggle.appendChild(icon);

      toggle.addEventListener("click", function () {
        block.classList.toggle("open");
        toggle.setAttribute("aria-expanded", block.classList.contains("open") ? "true" : "false");
      });
      block.appendChild(toggle);

      var items = document.createElement("div");
      items.className = "nav-group-items";

      group.items.forEach(function (item) {
        var link = document.createElement("a");
        link.href = item.href;
        link.textContent = item.label;
        link.setAttribute("data-nav-item", "");
        if (linkActive(item)) link.classList.add("active");
        items.appendChild(link);

        if (extraChildren && extraChildren.length && item.id === currentModule) {
          extraChildren.forEach(function (child) {
            var nested = document.createElement("a");
            nested.href = child.href;
            nested.className = "nav-child";
            nested.setAttribute("data-nav-item", "");
            nested.textContent = child.label;
            items.appendChild(nested);
          });
        }
      });

      block.appendChild(items);
      root.appendChild(block);
    });

    search.addEventListener("input", function () {
      var query = search.value.trim().toLowerCase();
      root.querySelectorAll(".nav-group").forEach(function (group) {
        var matches = 0;
        group.querySelectorAll("[data-nav-item]").forEach(function (item) {
          var show = !query || item.textContent.toLowerCase().includes(query);
          item.classList.toggle("hidden", !show);
          if (show) matches += 1;
        });
        group.classList.toggle("hidden", query && matches === 0);
        if (query && matches > 0) {
          group.classList.add("open");
          var toggleBtn = group.querySelector(".nav-group-toggle");
          if (toggleBtn) toggleBtn.setAttribute("aria-expanded", "true");
        }
      });
    });
  };

  if (!window.__chttpxSlashBound) {
    window.__chttpxSlashBound = true;
    document.addEventListener("keydown", function (event) {
      var search = document.querySelector("[data-sidebar-search]");
      if (!search) return;
      if (event.key === "/" && document.activeElement !== search &&
          document.activeElement.tagName !== "INPUT" &&
          document.activeElement.tagName !== "TEXTAREA") {
        event.preventDefault();
        search.focus();
      }
    });
  }

  if (sidebar && currentPage !== "module.html") {
    window.renderDocsSidebar();
  }

  function escapeHtml(text) {
    return text
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#39;");
  }

  function highlightC(source) {
    var escaped = escapeHtml(source);
    var parts = escaped.split(/(\/\*[\s\S]*?\*\/|\/\/[^\n]*|&quot;(?:\\.|[^&])*?&quot;|&#39;(?:\\.|[^&])*?&#39;)/g);
    return parts.map(function (part, index) {
      if (index % 2 === 1) {
        if (part.indexOf("//") === 0 || part.indexOf("/*") === 0) {
          return '<span class="token-cm">' + part + "</span>";
        }
        return '<span class="token-str">' + part + "</span>";
      }
      return part
        .replace(/\b(auto|break|case|const|continue|default|do|else|enum|extern|for|goto|if|inline|return|sizeof|static|struct|switch|typedef|union|volatile|while|bool|true|false|NULL)\b/g, '<span class="token-kw">$1</span>')
        .replace(/\b(void|char|short|int|long|float|double|unsigned|signed|size_t|ssize_t|uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|bool)\b/g, '<span class="token-type">$1</span>')
        .replace(/\b(0x[0-9a-fA-F]+|\d+)\b/g, '<span class="token-num">$1</span>');
    }).join("");
  }

  document.querySelectorAll("pre code").forEach(function (block) {
    if (block.querySelector("span")) return;
    block.innerHTML = highlightC(block.textContent);
  });

  window.highlightCodeBlocks = function (root) {
    (root || document).querySelectorAll("pre code").forEach(function (block) {
      if (block.querySelector("span")) return;
      block.innerHTML = highlightC(block.textContent);
    });
  };

  document.querySelectorAll("[data-copy-target]").forEach(function (button) {
    button.addEventListener("click", function () {
      var target = document.getElementById(button.getAttribute("data-copy-target"));
      if (!target) return;
      navigator.clipboard.writeText(target.innerText).then(function () {
        var oldText = button.textContent;
        button.textContent = "Copied";
        window.setTimeout(function () {
          button.textContent = oldText;
        }, 1300);
      }).catch(function () {
        button.textContent = "Select & copy";
      });
    });
  });

  var filter = document.querySelector("[data-doc-filter]");
  if (filter) {
    filter.addEventListener("input", function () {
      var query = filter.value.trim().toLowerCase();
      document.querySelectorAll("[data-doc-item]").forEach(function (item) {
        item.classList.toggle("hidden", query && !item.textContent.toLowerCase().includes(query));
      });
    });
  }

  function setStat(name, value) {
    document.querySelectorAll('[data-repo-stat="' + name + '"]').forEach(function (node) {
      node.textContent = value;
    });
  }

  if (document.querySelector("[data-repo-stat]")) {
    fetch(API, { headers: { Accept: "application/vnd.github+json" } })
      .then(function (response) {
        if (!response.ok) throw new Error("GitHub API error");
        return response.json();
      })
      .then(function (repo) {
        setStat("stars", repo.stargazers_count.toLocaleString());
        setStat("forks", repo.forks_count.toLocaleString());
        setStat("issues", repo.open_issues_count.toLocaleString());
      })
      .catch(function () {
        setStat("stars", "—");
        setStat("forks", "—");
        setStat("issues", "—");
      });

    fetch(API + "/releases/latest", { headers: { Accept: "application/vnd.github+json" } })
      .then(function (response) {
        if (!response.ok) throw new Error("No release");
        return response.json();
      })
      .then(function (release) {
        setStat("release", release.tag_name || "Latest");
      })
      .catch(function () {
        setStat("release", "—");
      });
  }

  document.querySelectorAll("[data-year]").forEach(function (node) {
    node.textContent = new Date().getFullYear();
  });
})();
