(function () {
  "use strict";

  var REPO = "netcorelink/libchttpx";
  var API = "https://api.github.com/repos/" + REPO;

  var menuButton = document.querySelector("[data-menu-button]");
  var navLinks = document.querySelector("[data-nav-links]");

  if (menuButton && navLinks) {
    menuButton.addEventListener("click", function () {
      var open = navLinks.classList.toggle("open");
      menuButton.setAttribute("aria-expanded", String(open));
    });
  }

  var currentPage = window.location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll("[data-nav-link]").forEach(function (link) {
    var href = link.getAttribute("href");
    if (href === currentPage || (currentPage === "" && href === "index.html")) {
      link.classList.add("active");
    }
  });

  document.querySelectorAll("[data-copy-target]").forEach(function (button) {
    button.addEventListener("click", function () {
      var targetId = button.getAttribute("data-copy-target");
      var target = document.getElementById(targetId);
      if (!target) {
        return;
      }

      var text = target.innerText;
      navigator.clipboard.writeText(text).then(function () {
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
        var text = item.textContent.toLowerCase();
        item.classList.toggle("hidden", query && !text.includes(query));
      });
    });
  }

  function setStat(name, value) {
    document.querySelectorAll('[data-repo-stat="' + name + '"]').forEach(function (node) {
      node.textContent = value;
    });
  }

  fetch(API, { headers: { Accept: "application/vnd.github+json" } })
    .then(function (response) {
      if (!response.ok) {
        throw new Error("GitHub API error");
      }
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
      if (!response.ok) {
        throw new Error("No release");
      }
      return response.json();
    })
    .then(function (release) {
      setStat("release", release.tag_name || "Latest");
    })
    .catch(function () {
      setStat("release", "—");
    });

  document.querySelectorAll("[data-year]").forEach(function (node) {
    node.textContent = new Date().getFullYear();
  });
})();
