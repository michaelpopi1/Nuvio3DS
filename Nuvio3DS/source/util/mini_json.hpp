#pragma once

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace nuvio::json {

enum class Type {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

struct Value {
    Type type = Type::Null;
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
    std::vector<Value> arrVal;
    std::map<std::string, Value> objVal;

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    std::string asString(const std::string& fallback = "") const {
        return type == Type::String ? strVal : fallback;
    }

    double asNumber(double fallback = 0.0) const {
        return type == Type::Number ? numVal : fallback;
    }

    int asInt(int fallback = 0) const {
        return type == Type::Number ? static_cast<int>(numVal) : fallback;
    }

    bool asBool(bool fallback = false) const {
        return type == Type::Bool ? boolVal : fallback;
    }

    const Value& get(const std::string& key) const {
        static const Value kNull{};
        if (type != Type::Object) return kNull;
        auto it = objVal.find(key);
        return it == objVal.end() ? kNull : it->second;
    }

    bool has(const std::string& key) const {
        return type == Type::Object && objVal.find(key) != objVal.end();
    }
};

class Parser {
public:
    explicit Parser(const std::string& input) : src_(input), pos_(0) {}

    bool parse(Value& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        return pos_ == src_.size();
    }

private:
    const std::string& src_;
    size_t pos_;

    void skipWs() {
        while (pos_ < src_.size() &&
               (src_[pos_] == ' ' || src_[pos_] == '\t' || src_[pos_] == '\n' || src_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool match(char c) {
        if (pos_ < src_.size() && src_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parseValue(Value& out) {
        skipWs();
        if (pos_ >= src_.size()) return false;
        char c = src_[pos_];
        if (c == 'n') return parseNull(out);
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == '"') {
            out = Value{};
            out.type = Type::String;
            return parseString(out.strVal);
        }
        if (c == '[') return parseArray(out);
        if (c == '{') return parseObject(out);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber(out);
        return false;
    }

    bool parseNull(Value& out) {
        if (src_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out = Value{};
            out.type = Type::Null;
            return true;
        }
        return false;
    }

    bool parseBool(Value& out) {
        if (src_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = Value{};
            out.type = Type::Bool;
            out.boolVal = true;
            return true;
        }
        if (src_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = Value{};
            out.type = Type::Bool;
            out.boolVal = false;
            return true;
        }
        return false;
    }

    bool parseNumber(Value& out) {
        const char* start = src_.c_str() + pos_;
        char* end = nullptr;
        double val = std::strtod(start, &end);
        if (end == start) return false;
        pos_ += static_cast<size_t>(end - start);
        out = Value{};
        out.type = Type::Number;
        out.numVal = val;
        return true;
    }

    bool parseString(std::string& out) {
        if (!match('"')) return false;
        out.clear();
        while (pos_ < src_.size()) {
            char c = src_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= src_.size()) return false;
                char esc = src_[pos_++];
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) return false;
                        unsigned int cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else return false;
                        }
                        if (cp < 0x80) {
                            out.push_back(static_cast<char>(cp));
                        } else if (cp < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                        }
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool parseArray(Value& out) {
        if (!match('[')) return false;
        out = Value{};
        out.type = Type::Array;
        skipWs();
        if (match(']')) return true;
        while (true) {
            Value elem;
            if (!parseValue(elem)) return false;
            out.arrVal.push_back(static_cast<Value&&>(elem));
            skipWs();
            if (match(']')) return true;
            if (!match(',')) return false;
        }
    }

    bool parseObject(Value& out) {
        if (!match('{')) return false;
        out = Value{};
        out.type = Type::Object;
        skipWs();
        if (match('}')) return true;
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (!match(':')) return false;
            Value val;
            if (!parseValue(val)) return false;
            out.objVal[key] = static_cast<Value&&>(val);
            skipWs();
            if (match('}')) return true;
            if (!match(',')) return false;
        }
    }
};

inline bool parse(const std::string& raw, Value& out) {
    Parser p(raw);
    return p.parse(out);
}

inline std::string escapeString(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    out.push_back('"');
    return out;
}

inline std::string stringify(const Value& v) {
    switch (v.type) {
        case Type::Null: return "null";
        case Type::Bool: return v.boolVal ? "true" : "false";
        case Type::Number: {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.6g", v.numVal);
            return std::string(buf);
        }
        case Type::String: return escapeString(v.strVal);
        case Type::Array: {
            std::string out = "[";
            for (size_t i = 0; i < v.arrVal.size(); ++i) {
                if (i > 0) out += ",";
                out += stringify(v.arrVal[i]);
            }
            out += "]";
            return out;
        }
        case Type::Object: {
            std::string out = "{";
            bool first = true;
            for (const auto& kv : v.objVal) {
                if (!first) out += ",";
                first = false;
                out += escapeString(kv.first) + ":" + stringify(kv.second);
            }
            out += "}";
            return out;
        }
    }
    return "null";
}

} // namespace nuvio::json
