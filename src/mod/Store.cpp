#include "mod/Store.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace invlink {

namespace {
std::string readFile(fs::path const& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + file.string());
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

bool safeUuid(std::string const& uuid) {
    static std::regex const re("[0-9a-fA-F-]{8,64}");
    return std::regex_match(uuid, re);
}

constexpr char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace

Store::Store(fs::path dir, std::string server, int keepHistory)
: mDir(std::move(dir)),
  mServer(std::move(server)),
  mKeepHistory(keepHistory) {
    for (auto sub : {"players", "alive"}) fs::create_directories(mDir / sub);
}

fs::path Store::recordFile(std::string const& uuid) const {
    if (!safeUuid(uuid)) throw std::invalid_argument("bad uuid");
    return mDir / "players" / (uuid + ".json");
}

void Store::writeWhole(fs::path const& file, std::string const& content) const {
    fs::path tmp = file;
    tmp += ".tmp-" + mServer;
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("cannot write " + tmp.string());
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) throw std::runtime_error("write failed " + tmp.string());
    }
    fs::rename(tmp, file); // replaces the old file in one step
}

std::optional<Record> Store::read(std::string const& uuid) const {
    fs::path file = recordFile(uuid);
    if (!fs::exists(file)) return std::nullopt;
    auto j = nlohmann::json::parse(readFile(file)); // throws on a broken file: the caller must not guess
    Record r;
    r.version    = j.at("version").get<std::uint64_t>();
    r.owner      = j.value("owner", "");
    r.ownerSince = j.value("ownerSince", std::int64_t{0});
    r.savedAt    = j.value("savedAt", std::int64_t{0});
    r.savedBy    = j.value("savedBy", "");
    r.name       = j.value("name", "");
    r.data       = unbase64(j.at("data").get<std::string>());
    return r;
}

void Store::write(std::string const& uuid, Record const& record) const {
    fs::path file = recordFile(uuid);
    if (fs::exists(file) && mKeepHistory > 0) {
        fs::path history = mDir / "players" / (uuid + ".history");
        fs::create_directories(history);
        try {
            auto old = nlohmann::json::parse(readFile(file));
            auto v   = old.value("version", std::uint64_t{0});
            fs::copy_file(file, history / (std::to_string(v) + ".json"), fs::copy_options::overwrite_existing);
        } catch (...) {
            fs::copy_file(file, history / "broken.json", fs::copy_options::overwrite_existing);
        }
        std::vector<fs::path> files;
        for (auto const& e : fs::directory_iterator(history)) files.push_back(e.path());
        auto num = [](fs::path const& p) {
            try { return std::stoull(p.stem().string()); } catch (...) { return 0ULL; }
        };
        std::sort(files.begin(), files.end(), [&](auto const& a, auto const& b) { return num(a) < num(b); });
        while (static_cast<int>(files.size()) > mKeepHistory) {
            fs::remove(files.front());
            files.erase(files.begin());
        }
    }
    nlohmann::json j{
        {"version", record.version},
        {"owner", record.owner},
        {"ownerSince", record.ownerSince},
        {"savedAt", record.savedAt},
        {"savedBy", record.savedBy},
        {"name", record.name},
        {"data", base64(record.data)}
    };
    writeWhole(file, j.dump(1));
}

bool Store::keepFirstBackup(std::string const& uuid, std::string const& binaryNbt) const {
    fs::path dir = mDir / "backup" / mServer;
    fs::path file = dir / (uuid + ".nbt");
    if (!safeUuid(uuid) || fs::exists(file)) return false;
    fs::create_directories(dir);
    writeWhole(file, binaryNbt);
    return true;
}

void Store::heartbeat(std::int64_t nowMs) const { writeWhole(mDir / "alive" / mServer, std::to_string(nowMs)); }

bool Store::alive(std::string const& server, std::int64_t nowMs, std::int64_t timeoutMs) const {
    try {
        auto t = std::stoll(readFile(mDir / "alive" / server));
        return nowMs - t <= timeoutMs;
    } catch (...) {
        return false;
    }
}

std::string Store::base64(std::string const& raw) {
    std::string out;
    out.reserve((raw.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < raw.size(); i += 3) {
        unsigned v = (static_cast<unsigned char>(raw[i]) << 16) | (static_cast<unsigned char>(raw[i + 1]) << 8)
                   | static_cast<unsigned char>(raw[i + 2]);
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += kB64[(v >> 6) & 63];
        out += kB64[v & 63];
    }
    if (i < raw.size()) {
        unsigned v = static_cast<unsigned char>(raw[i]) << 16;
        if (i + 1 < raw.size()) v |= static_cast<unsigned char>(raw[i + 1]) << 8;
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < raw.size() ? kB64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

std::string Store::unbase64(std::string const& text) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::string out;
    unsigned    buf = 0;
    int         bits = 0;
    for (char c : text) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) throw std::runtime_error("bad base64");
        buf = (buf << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
    }
    return out;
}

} // namespace invlink
