#include "library.h"
#include "config.h"
#include <SD.h>
#include <esp_heap_caps.h>

Library g_lib;

static void* bigAlloc(size_t n) {
  void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
  if (!p) p = malloc(n);
  return p;
}

template <typename T>
static const char* loadTable(const char* path, T*& out, uint32_t& count) {
  File f = SD.open(path, FILE_READ);
  if (!f) return "Index file missing";
  size_t sz = f.size();
  count = sz / sizeof(T);
  if (count == 0) { f.close(); return "Index is empty"; }
  size_t bytes = (size_t)count * sizeof(T);
  out = (T*)bigAlloc(bytes);
  if (!out) { f.close(); return "Out of memory"; }
  uint8_t* dst = (uint8_t*)out;
  size_t off = 0;
  while (off < bytes) {
    size_t n = f.read(dst + off, min((size_t)8192, bytes - off));
    if (n == 0) break;
    off += n;
    yield();
  }
  f.close();
  return off == bytes ? nullptr : "Index read error";
}

bool Library::begin() {
  const char* e;
  if ((e = loadTable(INDEX_DIR "/artists.bin", _ar, nArtists))) { _err = e; return false; }
  if ((e = loadTable(INDEX_DIR "/albums.bin",  _al, nAlbums)))  { _err = e; return false; }
  if ((e = loadTable(INDEX_DIR "/tracks.bin",  _tr, nTracks)))  { _err = e; return false; }
  Serial.printf("[lib] %u artists, %u albums, %u tracks\n",
                (unsigned)nArtists, (unsigned)nAlbums, (unsigned)nTracks);
  return true;
}
