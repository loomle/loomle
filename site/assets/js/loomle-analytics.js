(function () {
  "use strict";

  var config = window.LOOMLE_ANALYTICS || {};
  var ga4Id = String(config.ga4Id || "").trim();
  var cloudflareBeaconToken = String(config.cloudflareBeaconToken || "").trim();
  var consentKey = String(config.consentStorageKey || "loomle.analytics.consent");
  var trackedHost = window.location.hostname === "loomle.ai" || window.location.hostname === "www.loomle.ai";
  var banner = null;
  var googleLoaded = false;

  function readConsent() {
    try {
      var value = window.localStorage.getItem(consentKey);
      return value === "granted" || value === "denied" ? value : null;
    } catch (_error) {
      return null;
    }
  }

  function writeConsent(value) {
    try {
      window.localStorage.setItem(consentKey, value);
    } catch (_error) {
      // A blocked storage API should not block the rest of the site.
    }
  }

  function gtag() {
    window.dataLayer = window.dataLayer || [];
    window.dataLayer.push(arguments);
  }

  function clearGoogleCookies() {
    var domain = window.location.hostname;
    document.cookie.split(";").forEach(function (cookie) {
      var name = cookie.split("=")[0].trim();
      if (name !== "_ga" && name.indexOf("_ga_") !== 0) return;

      document.cookie = name + "=; Max-Age=0; path=/; SameSite=Lax";
      document.cookie = name + "=; Max-Age=0; path=/; domain=" + domain + "; SameSite=Lax";
      document.cookie = name + "=; Max-Age=0; path=/; domain=." + domain + "; SameSite=Lax";
    });
  }

  function configureGoogle() {
    if (!ga4Id || !trackedHost) return;

    window["ga-disable-" + ga4Id] = false;
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
    script.async = true;
    script.src = "https://static.cloudflareinsights.com/beacon.min.js";
    script.setAttribute("data-cf-beacon", JSON.stringify({ token: cloudflareBeaconToken }));
    document.head.appendChild(script);
  }

  function disableGoogle() {
    if (!ga4Id) return;
    window["ga-disable-" + ga4Id] = true;
    clearGoogleCookies();
  }

  function track(name, parameters) {
    if (!ga4Id || !trackedHost || readConsent() !== "granted") return;
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

  function removeBanner() {
    if (!banner) return;
    banner.remove();
    banner = null;
  }

  function setConsent(value) {
    writeConsent(value);
    if (value === "granted") configureGoogle();
    else disableGoogle();
    removeBanner();
    document.dispatchEvent(new CustomEvent("loomle:analytics-consent", { detail: value }));
  }

  function showPreferences() {
    if (!ga4Id || !trackedHost || banner) return;

    banner = document.createElement("section");
    banner.className = "loom-consent";
    banner.setAttribute("role", "dialog");
    banner.setAttribute("aria-label", "Optional analytics preferences");
    banner.innerHTML =
      '<div class="loom-consent-copy">' +
        '<strong>Optional analytics</strong>' +
        '<p>We use cookie-free Cloudflare analytics for aggregate traffic. With your permission, Google Analytics helps us understand sources and improve Loomle. <a href="/privacy/">Privacy details</a>.</p>' +
      '</div>' +
      '<div class="loom-consent-actions">' +
        '<button type="button" data-consent="denied">No optional analytics</button>' +
        '<button type="button" class="loom-consent-allow" data-consent="granted">Allow Google Analytics</button>' +
      '</div>';

    banner.addEventListener("click", function (event) {
      var button = event.target.closest("button[data-consent]");
      if (button) setConsent(button.getAttribute("data-consent"));
    });

    document.body.appendChild(banner);
    banner.querySelector("button[data-consent]").focus();
  }

  document.addEventListener("click", function (event) {
    var preferencesButton = event.target.closest("[data-analytics-preferences]");
    if (preferencesButton) {
      event.preventDefault();
      showPreferences();
      return;
    }
    handleTrackedClick(event);
  });

  window.loomleAnalytics = {
    getConsent: readConsent,
    setConsent: setConsent,
    showPreferences: showPreferences,
    track: track
  };

  configureCloudflare();
  if (readConsent() === "granted") configureGoogle();
  else if (readConsent() === null && ga4Id && trackedHost) showPreferences();
})();
