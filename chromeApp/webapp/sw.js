const version = 8; // increment version to force local cache flush of wasm files

var cacheName = 'MicroBlocks';
var filesToCache = [
  './',
  './microblocks.html',
  './emModule.js',
  './gpSupport.js',
  './badgesnake-boardie.js',
  './FileSaver.js',
  './gp_wasm.js',
  './gp_wasm.wasm',
  './gp_wasm.data',
];

/* Start the service worker and cache all of the app's content */
self.addEventListener('install', function(e) {
  e.waitUntil(
    caches.open(cacheName).then(function(cache) {
      return cache.addAll(filesToCache);
    })
  );
  self.skipWaiting();
});

/* Serve cached content when offline */
self.addEventListener('fetch', function(e) {
  e.respondWith(
    caches.match(e.request).then(function(response) {
      return response || fetch(e.request);
    })
  );
});
