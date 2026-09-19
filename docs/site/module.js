(function () {
  "use strict";

  var modules = [
    ["app", "App runtime"],
    ["server", "Server"],
    ["routing", "Routing"],
    ["middleware", "Middleware"],
    ["request", "Request"],
    ["responses", "Responses"],
    ["json", "JSON"],
    ["memory", "Memory"],
    ["uploads", "Uploads"],
    ["cors", "CORS"],
    ["cookies", "Cookies"],
    ["i18n", "Request IDs & i18n"],
    ["logging", "Logging"],
    ["rate-limiting", "Rate limiting"],
    ["websocket", "WebSocket"]
  ];

  var allowed = {};
  modules.forEach(function (item) { allowed[item[0]] = item[1]; });

  var params = new URLSearchParams(window.location.search);
  var name = params.get("name") || "app";
  if (!allowed[name]) name = "app";

  var container = document.querySelector("[data-module-content]");
  var breadcrumb = document.querySelector("[data-module-breadcrumb]");
  var nav = document.querySelector("[data-module-nav]");

  document.title = allowed[name] + " — libchttpx";
  if (breadcrumb) breadcrumb.textContent = allowed[name];

  if (nav) {
    modules.forEach(function (item) {
      var link = document.createElement("a");
      link.href = "module.html?name=" + encodeURIComponent(item[0]);
      link.textContent = item[1];
      if (item[0] === name) link.classList.add("active");
      nav.appendChild(link);
    });
  }

  function rewriteLinks(root) {
    root.querySelectorAll("a[href]").forEach(function (link) {
      var href = link.getAttribute("href") || "";
      var match = href.match(/^\.\.\/([^/]+)\/README\.md(?:#(.*))?$/);
      if (match && allowed[match[1]]) {
        link.href = "module.html?name=" + encodeURIComponent(match[1]) + (match[2] ? "#" + match[2] : "");
        return;
      }
      if (href === "../README.md" || href === "README.md") {
        link.href = "docs.html";
        return;
      }
      if (/^https?:\/\//.test(href)) {
        link.target = "_blank";
        link.rel = "noopener noreferrer";
      }
    });
  }

  fetch("content/" + encodeURIComponent(name) + ".md")
    .then(function (response) {
      if (!response.ok) throw new Error("Documentation file not found");
      return response.text();
    })
    .then(function (markdown) {
      if (!window.marked) throw new Error("Markdown renderer failed to load");
      container.innerHTML = window.marked.parse(markdown, { gfm: true, breaks: false });
      rewriteLinks(container);
      if (window.location.hash) {
        window.requestAnimationFrame(function () {
          var target = document.querySelector(window.location.hash);
          if (target) target.scrollIntoView();
        });
      }
    })
    .catch(function () {
      container.innerHTML = '<div class="doc-error">Could not load this documentation. <a href="docs.html">Return to documentation</a>.</div>';
    });
})();