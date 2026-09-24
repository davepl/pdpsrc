# Preserved website designs

These are restoration sources, not additional public pages. Both deployment
tools preserve them under `/usr/src/local/webtop/archive`; the native installer
publishes only the files in `site/`.

## Original red-and-gold site

`pre-webtop-192.168.1.26/index.html` and `pdp1183.jpg` are byte-for-byte copies
of the old `/var/www` content, backed up before the September 20 deployment.
The original HTML is 17,983 bytes; the original photograph is 106,717 bytes.
They were recovered from `previous-webroot.tar`, also preserved on the PDP at:

```
/usr/src/local/webtop/backups/Sun_Sep_20_15_00_06_PDT_2026-201/previous-webroot.tar
```

The current `site/index.source.html` adapts this layout with TOP above the
original content, the session-based site-wide counter, updated M11 references,
and no public telnet invitation. The published `site/pdp1183-web.jpg` is a lossless
JPEG optimization of the original: 101,511 bytes, still 640×360 pixels. It was
produced with libjpeg-turbo `jpegtran -copy none -optimize -progressive`;
decoded PPM files from the original and optimized JPEG compare identically.
The new filename avoids stale copies of the larger image in browser/CDN caches.

## Previous amber webtop site

`amber-webtop/index.html` and `index.source.html` preserve the minified and
readable homepage immediately before the red-and-gold restoration. That page
has TOP and the visitor counter, with no main-page photograph.

The older `site/index.previous.html` and `site/pdp11.jpg` belong to the earlier
physical-machine deployment at `.29` and are retained separately.

## TMOG banner

`tmog-banner.original.png` is the unchanged 2172×724 CPU benchmark banner
supplied on September 20. The served `site/tmog-banner-v2.jpg` is resized to
1440×480 and encoded with macOS `sips` at JPEG quality 80 (167,696 bytes).
It appears immediately below TOP and links to `https://tmog.org/`. The new
filename avoids cached copies of the first banner, whose original image is
retained as `tmog-banner.first.png`.

These design archives do not contain the mutable visitor total. Keep runtime
backups separately, as described in the main README.

## Gary artwork

`gary/gary-original-v2.png` is the unchanged replacement supplied on September
24, 2026 (the second image, with a moustache). The portrait was scaled to
480×720 with macOS sips and encoded as JPEG quality 88. The 600×600 preview
uses a crop at x=200, y=80, encoded at quality 90; 180×180 and 32×32 PNG
icons are scaled from that crop. The older public Gary assets remain in
`site/` and on the web servers for rollback.
