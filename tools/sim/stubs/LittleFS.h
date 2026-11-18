#pragma once
// Sistema de arquivos simulado numa pasta do PC (SIM_FS)
#include <Arduino.h>
#include <dirent.h>
#include <sys/stat.h>
class File
{
public:
  FILE *f = nullptr; DIR *d = nullptr; std::string path, nm;
  operator bool() const { return f || d; }
  size_t size() { if(!f) return 0; long p = ftell(f); fseek(f, 0, SEEK_END); long s = ftell(f); fseek(f, p, SEEK_SET); return s; }
  size_t read(uint8_t *b, size_t n) { return f ? fread(b, 1, n, f) : 0; }
  int read() { return f ? fgetc(f) : -1; }
  size_t readBytes(char *b, size_t n) { return read((uint8_t *)b, n); }
  size_t write(uint8_t c) { return f ? (fputc(c, f) != EOF) : 0; }
  size_t write(const uint8_t *b, size_t n) { return f ? fwrite(b, 1, n, f) : 0; }
  bool seek(size_t o) { return f && fseek(f, o, SEEK_SET) == 0; }
  void close() { if(f) fclose(f); if(d) closedir(d); f = nullptr; d = nullptr; }
  const char *name() { return nm.c_str(); }
  File openNextFile()
  {
    File r; if(!d) return r;
    while(dirent *e = readdir(d)) { if(e->d_name[0] == '.') continue; r.nm = e->d_name; r.path = path + "/" + e->d_name; r.f = fopen(r.path.c_str(), "rb"); return r; }
    return r;
  }
};
struct FSStub
{
  std::string root();
  bool exists(const String &p) { struct stat st; return stat((root() + p).c_str(), &st) == 0; }
  bool mkdir(const String &p) { return ::mkdir((root() + p).c_str(), 0755) == 0; }
  bool remove(const String &p) { return ::remove((root() + p).c_str()) == 0; }
  bool rename(const String &a, const String &b) { return ::rename((root() + a).c_str(), (root() + b).c_str()) == 0; }
  File open(const String &p, const char *mode = "r")
  {
    File r; r.path = root() + p; r.nm = p.substr(p.rfind('/') + 1);
    struct stat st;
    if(stat(r.path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) { r.d = opendir(r.path.c_str()); return r; }
    r.f = fopen(r.path.c_str(), mode[0] == 'w' ? "wb" : "rb");
    return r;
  }
  size_t totalBytes() { return 8 << 20; } size_t usedBytes() { return 0; }
};
extern FSStub LittleFS;
