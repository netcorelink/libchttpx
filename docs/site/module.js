(function () {
  "use strict";

  var modules = {};
  if (window.CHTTPX_DOCS) {
    window.CHTTPX_DOCS.forEach(function (group) {
      group.items.forEach(function (item) {
        if (item.id) modules[item.id] = item.label;
      });
    });
  }

  var params = new URLSearchParams(window.location.search);
  var name = params.get("name") || "app";
  if (!modules[name]) name = "app";

  var container = document.querySelector("[data-module-content]");
  var breadcrumb = document.querySelector("[data-module-breadcrumb]");

  document.title = modules[name] + " — libchttpx";
  if (breadcrumb) breadcrumb.textContent = modules[name];

  function slugify(text) {
    return text.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
  }

  function headingChildren(root) {
    var children = [];
    root.querySelectorAll("h2").forEach(function (heading) {
      if (!heading.id) heading.id = slugify(heading.textContent);
      children.push({
        href: "#" + heading.id,
        label: heading.textContent
      });
    });
    return children;
  }

  function rewriteLinks(root) {
    root.querySelectorAll("a[href]").forEach(function (link) {
      var href = link.getAttribute("href") || "";
      var match = href.match(/^\.\.\/([^/]+)\/README\.md(?:#(.*))?$/);
      if (match && modules[match[1]]) {
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

  if (window.renderDocsSidebar) window.renderDocsSidebar();

  fetch("content/" + encodeURIComponent(name) + ".md")
    .then(function (response) {
      if (!response.ok) throw new Error("Documentation file not found");
      return response.text();
    })
    .then(function (markdown) {
      if (!window.marked) throw new Error("Markdown renderer failed to load");
      container.innerHTML = window.marked.parse(markdown, { gfm: true, breaks: false });
      rewriteLinks(container);
      if (window.highlightCodeBlocks) window.highlightCodeBlocks(container);
      if (window.renderDocsSidebar) window.renderDocsSidebar(headingChildren(container));
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
