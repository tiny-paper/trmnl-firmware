#pragma once

#include <string.h>

#include <map>
#include <string>
#include <vector>

#include <webshare_fs.h>

// An in-memory file system for the Wi-Fi page tests, with the faults the device has: a full disk, a failing
// rename, a read that fails halfway.
struct MemFs {
  struct Node {
    bool dir;
    std::string data;
  };
  std::map<std::string, Node> nodes;
  uint64_t capacity = 1u << 30; // bytes of file data
  long fail_write_after = -1; // total bytes accepted, then writes fail (disk full); -1 never
  bool fail_rename = false;
  bool fail_close = false;
  bool close_loses_last_byte = false; // LittleFS: the full disk shows only at close, and the wrapper hides it
  long fail_read_after = -1; // a file read fails after this many bytes of one file
  long written = 0;
  ws_fs_t fs;

  struct Handle {
    std::string path;
    bool write;
    size_t pos;
  };
  std::vector<Handle *> open_handles;

  MemFs();
  uint64_t used() const {
    uint64_t n = 0;
    for (auto &kv : nodes) n += kv.second.data.size();
    return n;
  }
  void put(const std::string &path, const std::string &data) { nodes[path] = Node{false, data}; }
  bool has(const std::string &path) const { return nodes.count(path) != 0; }
  std::string get(const std::string &path) const { return nodes.at(path).data; }
  std::vector<std::string> files() const {
    std::vector<std::string> v;
    for (auto &kv : nodes) if (!kv.second.dir) v.push_back(kv.first);
    return v;
  }
};

static inline MemFs *self(void *ctx) { return (MemFs *)ctx; }

inline MemFs::MemFs() {
  fs.ctx = this;
  fs.stat = [](void *c, const char *p, uint32_t *size, bool *dir) -> int {
    MemFs *m = self(c);
    auto it = m->nodes.find(p);
    if (it == m->nodes.end()) return -1;
    if (size) *size = (uint32_t)it->second.data.size();
    if (dir) *dir = it->second.dir;
    return 0;
  };
  fs.open = [](void *c, const char *p, bool write) -> void * {
    MemFs *m = self(c);
    auto it = m->nodes.find(p);
    if (write) {
      if (it != m->nodes.end() && it->second.dir) return nullptr;
      m->nodes[p] = Node{false, ""};
    } else if (it == m->nodes.end() || it->second.dir) {
      return nullptr;
    }
    Handle *h = new Handle{p, write, 0};
    m->open_handles.push_back(h);
    return h;
  };
  fs.read = [](void *c, void *f, void *buf, size_t len) -> int {
    MemFs *m = self(c);
    Handle *h = (Handle *)f;
    const std::string &d = m->nodes[h->path].data;
    if (m->fail_read_after >= 0 && (long)h->pos >= m->fail_read_after) return -1;
    size_t n = h->pos >= d.size() ? 0 : d.size() - h->pos;
    if (n > len) n = len;
    if (m->fail_read_after >= 0 && (long)(h->pos + n) > m->fail_read_after) n = (size_t)m->fail_read_after - h->pos;
    memcpy(buf, d.data() + h->pos, n);
    h->pos += n;
    return (int)n;
  };
  fs.write = [](void *c, void *f, const void *buf, size_t len) -> int {
    MemFs *m = self(c);
    Handle *h = (Handle *)f;
    if (m->fail_write_after >= 0 && m->written + (long)len > m->fail_write_after) return -1;
    if (m->used() + len > m->capacity) return -1;
    m->nodes[h->path].data.append((const char *)buf, len);
    m->written += (long)len;
    return (int)len;
  };
  fs.close = [](void *c, void *f) -> int {
    MemFs *m = self(c);
    Handle *h = (Handle *)f;
    for (size_t i = 0; i < m->open_handles.size(); i++) {
      if (m->open_handles[i] == h) m->open_handles.erase(m->open_handles.begin() + (long)i);
    }
    if (h->write && m->close_loses_last_byte && !m->nodes[h->path].data.empty()) m->nodes[h->path].data.pop_back();
    delete h;
    return m->fail_close ? -1 : 0;
  };
  fs.rename = [](void *c, const char *from, const char *to) -> int {
    MemFs *m = self(c);
    if (m->fail_rename || !m->nodes.count(from)) return -1;
    m->nodes[to] = m->nodes[from]; // replaces silently, like LittleFS
    m->nodes.erase(from);
    return 0;
  };
  fs.remove = [](void *c, const char *p) -> int { return self(c)->nodes.erase(p) ? 0 : -1; };
  fs.mkdir = [](void *c, const char *p) -> int {
    self(c)->nodes[p] = Node{true, ""};
    return 0;
  };
  fs.list = [](void *c, const char *dir, ws_list_cb_t cb, void *cx) -> int {
    MemFs *m = self(c);
    std::string prefix = std::string(dir) + "/";
    if (!m->nodes.count(dir)) return -1;
    int n = 0;
    for (auto &kv : m->nodes) {
      if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
      std::string rest = kv.first.substr(prefix.size());
      if (rest.find('/') != std::string::npos) continue;
      n++;
      if (!cb(rest.c_str(), kv.second.dir, (uint32_t)kv.second.data.size(), cx)) break;
    }
    return n;
  };
  fs.free_bytes = [](void *c) -> uint64_t {
    MemFs *m = self(c);
    return m->capacity > m->used() ? m->capacity - m->used() : 0;
  };
}
