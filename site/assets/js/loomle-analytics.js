(function () {
  "use strict";

  var config = window.LOOMLE_ANALYTICS || {};
  var ga4Id = String(config.ga4Id || "").trim();
  var cloudflareBeaconToken = String(config.cloudflareBeaconToken || "").trim();
  var trackedHost = window.location.hostname === "loomle.ai" || window.location.hostname === "www.loomle.ai";
  var googleLoaded = false;

  function gtag() {
    window.dataLayer = window.dataLayer || [];
    window.dataLayer.push(arguments);
  }

  function configureGoogle() {
    if (!ga4Id || !trackedHost) return;

    if (googleLoaded) return;
    googleLoaded = true;

    window.gtag = window.gtag || gtag;
    window.gtag("js", new Date());
    window.gtag("config", ga4Id, {
      allow_ad_personalization_signals: false,
      allow_google_signals: false
    });

    var script = document.createElement("script");
    script.async = true;
    script.src = "https://www.googletagmanager.com/gtag/js?id=" + encodeURIComponent(ga4Id);
    document.head.appendChild(script);
  }

  function configureCloudflare() {
    if (!cloudflareBeaconToken || !trackedHost) return;

    var script = document.createElement("script");
    script.type = "module";
    script.src = "https://static.cloudflareinsights.com/beacon.min.js";
    script.setAttribute("data-cf-beacon", JSON.stringify({ token: cloudflareBeaconToken }));
    document.head.appendChild(script);
  }

  function track(name, parameters) {
    if (!ga4Id || !trackedHost) return;
    configureGoogle();
    window.gtag("event", name, parameters || {});
  }

  function cleanLinkUrl(url) {
    return url.origin + url.pathname;
  }

  function classifyLink(link, url) {
    var explicitEvent = link.getAttribute("data-track");
    if (explicitEvent) return explicitEvent;

    var host = url.hostname.toLowerCase();
    var path = url.pathname.toLowerCase();
    var isDownload = link.hasAttribute("download") ||
      /\.(zip|dmg|pkg|exe|msi|sha256)$/.test(path) ||
      (host === "github.com" && path.indexOf("/releases/") !== -1 && path.indexOf("/download/") !== -1);

    if (isDownload) return "download_click";
    if (host === "fab.com" || host.endsWith(".fab.com")) return "fab_click";
    if (host === "github.com" || host.endsWith(".github.com")) return "github_click";
    if (url.origin === window.location.origin && path.indexOf("/quickstart") === 0) return "docs_start";
    if (url.origin === window.location.origin && path.indexOf("/install") === 0) return "install_click";
    if (url.origin === window.location.origin && path.indexOf("/blog/") === 0) return "blog_click";
    return null;
  }

  function handleTrackedClick(event) {
    var link = event.target.closest("a[href]");
    if (!link) return;

    var url;
    try {
      url = new URL(link.href, window.location.href);
    } catch (_error) {
      return;
    }

    var eventName = classifyLink(link, url);
    if (!eventName) return;

    track(eventName, {
      link_url: cleanLinkUrl(url),
      link_domain: url.hostname,
      link_text: (link.textContent || "").trim().replace(/\s+/g, " ").slice(0, 100)
    });
  }

  document.addEventListener("click", function (event) {
    handleTrackedClick(event);
  });

  window.loomleAnalytics = {
    track: track
  };

  configureCloudflare();
  configureGoogle();
})();
