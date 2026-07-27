#include "ExifWriter.h"
#include <cmath>
#include <string>

using catalog::PhotoRecord;

namespace export_ns {

// ── Minimal EXIF APP1 builder (little-endian TIFF) ───────────────────────────

namespace {

// Write helpers (little-endian)
static void writeU16LE(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
}
static void writeU32LE(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 8) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 16) & 0xFF));
  v.push_back(static_cast<uint8_t>((x >> 24) & 0xFF));
}
// Write an IFD entry (tag, type, count, value-or-offset)
static void writeIfdEntry(std::vector<uint8_t>& v, uint16_t tag, uint16_t type, uint32_t count,
                          uint32_t valueOrOffset) {
  writeU16LE(v, tag);
  writeU16LE(v, type);
  writeU32LE(v, count);
  writeU32LE(v, valueOrOffset);
}

// Append a null-terminated ASCII string padded to even length; return byte count including null
static uint32_t appendAscii(std::vector<uint8_t>& heap, const std::string& s) {
  const uint32_t len = static_cast<uint32_t>(s.size()) + 1;  // +1 for null
  for (char c : s) {
    heap.push_back(static_cast<uint8_t>(c));
  }
  heap.push_back(0);
  if (len % 2 != 0) {
    heap.push_back(0);  // pad to even
  }
  return len;
}

// Encode a decimal degree as DMS rational triple (3 RATIONAL values = 24 bytes)
// Returns offset into heap where the 24 bytes were written
static void appendGpsDms(std::vector<uint8_t>& heap, double deg) {
  if (deg < 0) {
    deg = -deg;
  }
  const uint32_t d = static_cast<uint32_t>(deg);
  deg = (deg - d) * 60.0;
  const uint32_t m = static_cast<uint32_t>(deg);
  deg = (deg - m) * 60.0;
  const uint32_t sNum = static_cast<uint32_t>(deg * 100);
  const uint32_t sDen = 100;

  // degrees: d/1
  writeU32LE(heap, d);
  writeU32LE(heap, 1);
  // minutes: m/1
  writeU32LE(heap, m);
  writeU32LE(heap, 1);
  // seconds: sNum/sDen
  writeU32LE(heap, sNum);
  writeU32LE(heap, sDen);
}

// Format capture time "YYYY-MM-DDTHH:MM:SS" → "YYYY:MM:DD HH:MM:SS"
static std::string captureTimeToExif(const std::string& iso) {
  // Expected input: "YYYY-MM-DDTHH:MM:SS" or "YYYY-MM-DD HH:MM:SS" or similar
  if (iso.size() < 19) {
    return "0000:00:00 00:00:00";
  }
  std::string out = iso.substr(0, 19);
  // Replace '-' in date part with ':'
  if (out[4] == '-') {
    out[4] = ':';
  }
  if (out[7] == '-') {
    out[7] = ':';
  }
  // Replace 'T' separator with space
  if (out[10] == 'T') {
    out[10] = ' ';
  }
  return out;
}

}  // namespace

// Build a complete EXIF APP1 payload (starting after the FF E1 marker + length)
// Returns the full payload including "Exif\0\0" header and TIFF block.
std::vector<uint8_t> buildExifPayload(const PhotoRecord& rec) {
  // Layout plan (little-endian TIFF):
  //   Offset 0: TIFF header (8 bytes): II + 0x002A + IFD0 offset
  //   IFD0 at offset 8: up to 4 entries (Make, Model, ExifIFD, [GPSIFD])
  //   ExifIFD: 1 entry (DateTimeOriginal)
  //   GPSIFD: if gps non-zero, 6 entries
  //   Heap: ASCII strings and rational values

  // We'll build the TIFF block first, resolve offsets later.
  // TIFF base offset = 0 (relative to start of TIFF block, after "Exif\0\0")

  const std::string exifDt = captureTimeToExif(rec.captureTime);
  const bool hasGps = (rec.gpsLat != 0.0 || rec.gpsLon != 0.0);
  const bool hasMake = !rec.cameraMake.empty();
  const bool hasModel = !rec.cameraModel.empty();

  // Count IFD0 entries
  int ifd0Count = 1;  // always ExifIFD pointer
  if (hasMake) {
    ++ifd0Count;
  }
  if (hasModel) {
    ++ifd0Count;
  }
  if (hasGps) {
    ++ifd0Count;
  }

  // Sizes:
  //   TIFF header:  8
  //   IFD0:         2 + ifd0Count*12 + 4
  //   ExifIFD:      2 + 1*12 + 4
  //   GPSIFD:       2 + 6*12 + 4  (if present)

  const size_t hdrSize = 8;
  const size_t ifd0Size = 2 + ifd0Count * 12 + 4;
  const size_t exifIfdOff = hdrSize + ifd0Size;
  const size_t exifIfdSize = 2 + 1 * 12 + 4;
  const size_t gpsIfdOff = exifIfdOff + exifIfdSize;
  const size_t gpsIfdSize = hasGps ? (2 + 7 * 12 + 4) : 0;  // 7 GPS entries
  const size_t heapStart = gpsIfdOff + gpsIfdSize;

  std::vector<uint8_t> tiff;
  tiff.reserve(512);
  std::vector<uint8_t> heap;  // string/rational data appended after IFDs

  // ── TIFF header ───────────────────────────────────────────────────────────
  tiff.push_back('I');
  tiff.push_back('I');                               // little-endian
  writeU16LE(tiff, 0x002A);                          // TIFF magic
  writeU32LE(tiff, static_cast<uint32_t>(hdrSize));  // IFD0 at offset 8

  // ── IFD0 ─────────────────────────────────────────────────────────────────
  writeU16LE(tiff, static_cast<uint16_t>(ifd0Count));

  // Helper: offset into heap = heapStart + heap.size() at time of call
  // We'll accumulate heap separately and compute offsets accordingly.

  if (hasMake) {
    const uint32_t off = static_cast<uint32_t>(heapStart + heap.size());
    const uint32_t len = appendAscii(heap, rec.cameraMake);
    writeIfdEntry(tiff, 0x010F, 2, len, off);  // Make, ASCII
  }
  if (hasModel) {
    const uint32_t off = static_cast<uint32_t>(heapStart + heap.size());
    const uint32_t len = appendAscii(heap, rec.cameraModel);
    writeIfdEntry(tiff, 0x0110, 2, len, off);  // Model, ASCII
  }
  // ExifIFD pointer
  writeIfdEntry(tiff, 0x8769, 4, 1, static_cast<uint32_t>(exifIfdOff));
  // GPSIFD pointer (if GPS)
  if (hasGps) {
    writeIfdEntry(tiff, 0x8825, 4, 1, static_cast<uint32_t>(gpsIfdOff));
  }
  writeU32LE(tiff, 0);  // IFD0 next-IFD = none

  // ── ExifIFD ───────────────────────────────────────────────────────────────
  writeU16LE(tiff, 1);  // 1 entry: DateTimeOriginal
  {
    const uint32_t off = static_cast<uint32_t>(heapStart + heap.size());
    const uint32_t len = appendAscii(heap, exifDt);
    writeIfdEntry(tiff, 0x9003, 2, len, off);  // DateTimeOriginal, ASCII
  }
  writeU32LE(tiff, 0);  // ExifIFD next-IFD = none

  // ── GPSIFD ────────────────────────────────────────────────────────────────
  if (hasGps) {
    writeU16LE(tiff, 7);  // 7 entries: VersionID, Lat/Ref, Lon/Ref, Alt/Ref

    // GPSVersionID: BYTE[4] = {2,3,0,0} — fits inline
    writeU16LE(tiff, 0x0000);
    writeU16LE(tiff, 1);
    writeU32LE(tiff, 4);
    tiff.push_back(2);
    tiff.push_back(3);
    tiff.push_back(0);
    tiff.push_back(0);

    // GPSLatitudeRef: ASCII[2] — fits inline (must not use an offset)
    {
      const uint8_t ref = rec.gpsLat >= 0 ? 'N' : 'S';
      writeU16LE(tiff, 0x0001);
      writeU16LE(tiff, 2);  // ASCII
      writeU32LE(tiff, 2);  // count (incl. null)
      tiff.push_back(ref);
      tiff.push_back(0);
      tiff.push_back(0);
      tiff.push_back(0);
    }
    // GPSLatitude: RATIONAL[3]
    {
      const uint32_t off = static_cast<uint32_t>(heapStart + heap.size());
      appendGpsDms(heap, rec.gpsLat);
      writeIfdEntry(tiff, 0x0002, 5, 3, off);
    }
    // GPSLongitudeRef: ASCII[2] — fits inline (must not use an offset)
    {
      const uint8_t ref = rec.gpsLon >= 0 ? 'E' : 'W';
      writeU16LE(tiff, 0x0003);
      writeU16LE(tiff, 2);  // ASCII
      writeU32LE(tiff, 2);  // count (incl. null)
      tiff.push_back(ref);
      tiff.push_back(0);
      tiff.push_back(0);
      tiff.push_back(0);
    }
    // GPSLongitude: RATIONAL[3]
    {
      const uint32_t off = static_cast<uint32_t>(heapStart + heap.size());
      appendGpsDms(heap, rec.gpsLon);
      writeIfdEntry(tiff, 0x0004, 5, 3, off);
    }
    // GPSAltitudeRef: BYTE[1] — 0=above sea, 1=below; fits inline
    {
      const uint8_t ref = rec.gpsAltM < 0 ? 1 : 0;
      writeU16LE(tiff, 0x0005);
      writeU16LE(tiff, 1);
      writeU32LE(tiff, 1);
      tiff.push_back(ref);
      tiff.push_back(0);
      tiff.push_back(0);
      tiff.push_back(0);
    }
    // GPSAltitude: RATIONAL[1]
    {
      const uint32_t off = static_cast<uint32_t>(heapStart + heap.size());
      const double alt = std::abs(rec.gpsAltM);
      const uint32_t num = static_cast<uint32_t>(alt * 100);
      writeU32LE(heap, num);
      writeU32LE(heap, 100);
      writeIfdEntry(tiff, 0x0006, 5, 1, off);
    }
    writeU32LE(tiff, 0);  // GPSIFD next-IFD = none
  }

  // Append heap to tiff
  tiff.insert(tiff.end(), heap.begin(), heap.end());

  // Wrap in "Exif\0\0" prefix
  std::vector<uint8_t> payload;
  payload.reserve(6 + tiff.size());
  payload.push_back('E');
  payload.push_back('x');
  payload.push_back('i');
  payload.push_back('f');
  payload.push_back(0);
  payload.push_back(0);
  payload.insert(payload.end(), tiff.begin(), tiff.end());
  return payload;
}

// Inject an APP1 EXIF block right after the JPEG SOI marker (FF D8)
std::vector<uint8_t> injectExifApp1(const std::vector<uint8_t>& jpeg,
                                    const std::vector<uint8_t>& exifPayload) {
  if (jpeg.size() < 2 || jpeg[0] != 0xFF || jpeg[1] != 0xD8) {
    return jpeg;  // not a valid JPEG SOI
  }
  // APP1 block: FF E1 + 2-byte length (big-endian, includes length field) + payload
  const uint16_t app1Len = static_cast<uint16_t>(2 + exifPayload.size());
  std::vector<uint8_t> out;
  out.reserve(2 + 2 + 2 + exifPayload.size() + jpeg.size() - 2);

  // SOI
  out.push_back(0xFF);
  out.push_back(0xD8);
  // APP1 marker + length + payload
  out.push_back(0xFF);
  out.push_back(0xE1);
  out.push_back(static_cast<uint8_t>((app1Len >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>(app1Len & 0xFF));
  out.insert(out.end(), exifPayload.begin(), exifPayload.end());
  // Rest of original JPEG (skip SOI = first 2 bytes)
  out.insert(out.end(), jpeg.begin() + 2, jpeg.end());
  return out;
}

}  // namespace export_ns
