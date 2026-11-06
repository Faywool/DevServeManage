// Minimal JSON reader/writer used only for package.json, launchSettings.json and workspace.json.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdlib>
#include <cmath>
#include <cstdio>

namespace dsm {

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string text;
    std::vector<Json> array;
    std::vector<std::pair<std::string, Json>> object;

    const Json* find(const std::string& key) const {
        for (const auto& kv : object) if (kv.first == key) return &kv.second;
        return nullptr;
    }
    std::string str(const std::string& key, const std::string& def = {}) const {
        auto* v = find(key); return v && v->type == Type::String ? v->text : def;
    }
    int integer(const std::string& key, int def = 0) const {
        auto* v = find(key); return v && v->type == Type::Number ? (int)std::llround(v->number) : def;
    }
    bool flag(const std::string& key, bool def = false) const {
        auto* v = find(key); return v && v->type == Type::Bool ? v->boolean : def;
    }
    const Json* obj(const std::string& key) const {
        auto* v = find(key); return v && v->type == Type::Object ? v : nullptr;
    }
    const Json* arr(const std::string& key) const {
        auto* v = find(key); return v && v->type == Type::Array ? v : nullptr;
    }

    static Json Parse(const std::string& src, std::string* error = nullptr) {
        size_t i = 0;
        Json out;
        if (!parseValue(src, i, out)) {
            if (error) *error = "JSON 解析失败";
            return Json{};
        }
        skip(src, i);
        if (i != src.size()) {
            if (error) *error = "JSON 末尾存在多余内容";
            return Json{};
        }
        return out;
    }

    std::string dump(int indent = 2) const {
        std::string out;
        write(out, indent, 0);
        out.push_back('\n');
        return out;
    }

private:
    static void skip(const std::string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    static bool parseValue(const std::string& s, size_t& i, Json& v) {
        skip(s, i);
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') return parseObject(s, i, v);
        if (c == '[') return parseArray(s, i, v);
        if (c == '"') { v.type = Type::String; return parseString(s, i, v.text); }
        if (c == 't' && s.compare(i, 4, "true") == 0) { v.type = Type::Bool; v.boolean = true; i += 4; return true; }
        if (c == 'f' && s.compare(i, 5, "false") == 0) { v.type = Type::Bool; v.boolean = false; i += 5; return true; }
        if (c == 'n' && s.compare(i, 4, "null") == 0) { v.type = Type::Null; i += 4; return true; }
        if (c == '-' || (c >= '0' && c <= '9')) {
            char* end = nullptr;
            v.type = Type::Number;
            v.number = std::strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) return false;
            i = (size_t)(end - s.c_str());
            return true;
        }
        return false;
    }
    static bool parseObject(const std::string& s, size_t& i, Json& v) {
        v.type = Type::Object; ++i; skip(s, i);
        if (i < s.size() && s[i] == '}') { ++i; return true; }
        while (i < s.size()) {
            skip(s, i);
            std::string key;
            if (!parseString(s, i, key)) return false;
            skip(s, i);
            if (i >= s.size() || s[i] != ':') return false;
            ++i;
            Json child;
            if (!parseValue(s, i, child)) return false;
            v.object.emplace_back(std::move(key), std::move(child));
            skip(s, i);
            if (i >= s.size()) return false;
            if (s[i] == '}') { ++i; return true; }
            if (s[i] != ',') return false;
            ++i;
        }
        return false;
    }
    static bool parseArray(const std::string& s, size_t& i, Json& v) {
        v.type = Type::Array; ++i; skip(s, i);
        if (i < s.size() && s[i] == ']') { ++i; return true; }
        while (i < s.size()) {
            Json child;
            if (!parseValue(s, i, child)) return false;
            v.array.push_back(std::move(child));
            skip(s, i);
            if (i >= s.size()) return false;
            if (s[i] == ']') { ++i; return true; }
            if (s[i] != ',') return false;
            ++i;
        }
        return false;
    }
    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp <= 0x7F) out.push_back((char)cp);
        else if (cp <= 0x7FF) {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }
    static bool hex4(const std::string& s, size_t pos, unsigned& out) {
        if (pos + 4 > s.size()) return false;
        out = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s[pos + i]; out <<= 4;
            if (c >= '0' && c <= '9') out |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    static bool parseString(const std::string& s, size_t& i, std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i; out.clear();
        while (i < s.size()) {
            unsigned char c = (unsigned char)s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= s.size()) return false;
                char e = s[i++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = 0;
                    if (!hex4(s, i, cp)) return false;
                    i += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        unsigned lo = 0; i += 2;
                        if (!hex4(s, i, lo)) return false;
                        i += 4;
                        if (lo >= 0xDC00 && lo <= 0xDFFF)
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: return false;
                }
            } else {
                out.push_back((char)c);
            }
        }
        return false;
    }
    void write(std::string& out, int indent, int depth) const {
        auto pad = [&](int d) { if (indent > 0) { out.push_back('\n'); out.append((size_t)(d * indent), ' '); } };
        switch (type) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += boolean ? "true" : "false"; break;
        case Type::Number: {
            char buf[64]; std::snprintf(buf, sizeof(buf), "%.15g", number); out += buf; break;
        }
        case Type::String: writeString(out, text); break;
        case Type::Array:
            out.push_back('[');
            for (size_t i = 0; i < array.size(); ++i) {
                if (i) out.push_back(',');
                pad(depth + 1);
                array[i].write(out, indent, depth + 1);
            }
            if (!array.empty()) pad(depth);
            out.push_back(']');
            break;
        case Type::Object:
            out.push_back('{');
            for (size_t i = 0; i < object.size(); ++i) {
                if (i) out.push_back(',');
                pad(depth + 1);
                writeString(out, object[i].first);
                out += ": ";
                object[i].second.write(out, indent, depth + 1);
            }
            if (!object.empty()) pad(depth);
            out.push_back('}');
            break;
        }
    }
    static void writeString(std::string& out, const std::string& s) {
        out.push_back('"');
        for (unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf;
                } else out.push_back((char)c);
            }
        }
        out.push_back('"');
    }
};

} // namespace dsm
