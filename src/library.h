#pragma once
#include <Arduino.h>

// On-disk record layouts written by tools/build_index.py (little endian).
// Tables are sorted so every "children" list is a contiguous range:
//   artists -> albums (firstAlbum..+albumCount) -> tracks (firstTrack..+trackCount)
#pragma pack(push, 1)
struct ArtistRec {            // 64 bytes
  uint32_t firstAlbum;
  uint16_t albumCount;
  uint16_t trackCount;
  char     name[56];
};
struct AlbumRec {             // 128 bytes
  uint32_t artistIdx;
  uint32_t firstTrack;
  uint16_t trackCount;
  uint16_t year;
  uint8_t  hasCover;          // /.dap/covers/<albumIdx>.jpg exists
  uint8_t  _pad[3];
  char     name[112];
};
struct TrackRec {             // 256 bytes
  uint32_t albumIdx;
  uint32_t artistIdx;
  uint16_t trackNo;
  uint16_t discNo;
  uint32_t durationSec;
  uint16_t year;
  uint16_t _rsv;
  char     title[80];
  char     path[156];         // absolute path on the SD card
};
#pragma pack(pop)
static_assert(sizeof(ArtistRec) == 64,  "ArtistRec size");
static_assert(sizeof(AlbumRec)  == 128, "AlbumRec size");
static_assert(sizeof(TrackRec)  == 256, "TrackRec size");

// The whole index is loaded into (PS)RAM once at boot (~256 B per track), so
// browsing never touches the SD card and cannot disturb audio reads.
class Library {
 public:
  bool begin();                         // false -> see error()
  const char* error() const { return _err; }

  uint32_t nArtists = 0, nAlbums = 0, nTracks = 0;
  const ArtistRec& artist(uint32_t i) const { return _ar[i]; }
  const AlbumRec&  album(uint32_t i)  const { return _al[i]; }
  const TrackRec&  track(uint32_t i)  const { return _tr[i]; }

 private:
  ArtistRec* _ar = nullptr;
  AlbumRec*  _al = nullptr;
  TrackRec*  _tr = nullptr;
  const char* _err = "";
};

extern Library g_lib;
