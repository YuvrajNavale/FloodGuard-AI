// Minimal JSON value, parser and number formatter (no external dependencies).
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace fg {

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type t = Null;
    bool b = false;
    double n = 0.0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    bool is_obj() const { return t == Obj; }
    bool is_arr() const { return t == Arr; }
    const Json* find(const std::string& k) const {
        if (t != Obj) return nullptr;
        for (auto& kv : o) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    bool has(const std::string& k) const { return find(k) != nullptr; }
    double num(const std::string& k, double d) const {
        const Json* j = find(k); return (j && j->t == Num) ? j->n : d;
    }
    int integer(const std::string& k, int d) const { return (int)num(k, (double)d); }
    std::string str(const std::string& k, const std::string& d) const {
        const Json* j = find(k); return (j && j->t == Str) ? j->s : d;
    }
    bool boolean(const std::string& k, bool d) const {
        const Json* j = find(k); return (j && j->t == Bool) ? j->b : d;
    }

    static Json parse(const std::string& text) {
        size_t i = 0;
        Json v = parse_value(text, i);
        skip_ws(text, i);
        if (i != text.size()) throw std::runtime_error("JSON: trailing characters");
        return v;
    }

private:
    static void skip_ws(const std::string& s, size_t& i) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    }
    static std::string parse_string(const std::string& s, size_t& i) {
        if (i >= s.size() || s[i] != '"') throw std::runtime_error("JSON: expected string");
        ++i; std::string out;
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\') {
                ++i; if (i >= s.size()) break;
                switch (s[i]) {
                    case 'n': out += '\n'; break; case 't': out += '\t'; break;
                    case 'r': out += '\r'; break; case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break; case 'u': out += '?'; i += 4; break;
                    default: out += s[i];
                }
            } else out += s[i];
            ++i;
        }
        if (i >= s.size()) throw std::runtime_error("JSON: unterminated string");
        ++i; return out;
    }
    static Json parse_value(const std::string& s, size_t& i) {
        skip_ws(s, i);
        if (i >= s.size()) throw std::runtime_error("JSON: unexpected end");
        Json v; char c = s[i];
        if (c == '{') {
            v.t = Obj; ++i; skip_ws(s, i);
            if (i < s.size() && s[i] == '}') { ++i; return v; }
            while (true) {
                skip_ws(s, i); std::string k = parse_string(s, i); skip_ws(s, i);
                if (i >= s.size() || s[i] != ':') throw std::runtime_error("JSON: expected ':'");
                ++i; v.o.emplace_back(k, parse_value(s, i)); skip_ws(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                throw std::runtime_error("JSON: expected ',' or '}'");
            }
        } else if (c == '[') {
            v.t = Arr; ++i; skip_ws(s, i);
            if (i < s.size() && s[i] == ']') { ++i; return v; }
            while (true) {
                v.a.push_back(parse_value(s, i)); skip_ws(s, i);
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                throw std::runtime_error("JSON: expected ',' or ']'");
            }
        } else if (c == '"') { v.t = Str; v.s = parse_string(s, i); }
        else if (s.compare(i, 4, "true") == 0) { v.t = Bool; v.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.t = Bool; v.b = false; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { v.t = Null; i += 4; }
        else {
            char* end = nullptr; v.n = std::strtod(s.c_str() + i, &end);
            if (end == s.c_str() + i) throw std::runtime_error("JSON: invalid token");
            v.t = Num; i = (size_t)(end - s.c_str());
        }
        return v;
    }
};

inline std::string jnum(double x, const char* fmt = "%.10g") {
    char buf[64]; std::snprintf(buf, sizeof buf, fmt, x); return buf;
}
inline std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if (c == '\n') o += "\\n";
        else o += c;
    }
    return o + "\"";
}

} // namespace fg
