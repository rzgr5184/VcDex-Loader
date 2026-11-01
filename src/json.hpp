#pragma once

// Minimal, dependency-free JSON parser used for the VcDex update manifest.

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::map<std::string, Value> object;

    bool IsNull() const { return type == Type::Null; }
    bool IsObject() const { return type == Type::Object; }
    bool IsArray() const { return type == Type::Array; }

    const Value* Find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    const Value& At(const std::string& key) const {
        static const Value nullValue;
        const Value* found = Find(key);
        return found ? *found : nullValue;
    }

    std::string AsString(const std::string& fallback = {}) const {
        return type == Type::String ? string : fallback;
    }
    double AsNumber(double fallback = 0.0) const {
        return type == Type::Number ? number : fallback;
    }
    bool AsBool(bool fallback = false) const {
        return type == Type::Bool ? boolean : fallback;
    }

    std::size_t Size() const { return array.size(); }
    const Value& operator[](std::size_t index) const {
        static const Value nullValue;
        return index < array.size() ? array[index] : nullValue;
    }
};

namespace detail {

class Parser {
public:
    Parser(const char* data, std::size_t size) : m_begin(data), m_ptr(data), m_end(data + size) {}

    bool ParseDocument(Value& out, std::string& error) {
        SkipWhitespaceAndBom();
        if (!ParseValue(out, 0)) {
            error = m_error.empty() ? "Invalid JSON" : m_error;
            return false;
        }
        SkipWhitespace();
        if (m_ptr != m_end) {
            error = "Trailing characters after JSON document";
            return false;
        }
        return true;
    }

private:
    const char* m_begin;
    const char* m_ptr;
    const char* m_end;
    std::string m_error;

    bool Fail(const std::string& message) {
        if (m_error.empty()) m_error = message;
        return false;
    }

    void SkipWhitespace() {
        while (m_ptr < m_end) {
            const char c = *m_ptr;
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_ptr;
            } else {
                break;
            }
        }
    }

    void SkipWhitespaceAndBom() {
        if (m_end - m_ptr >= 3 &&
            static_cast<unsigned char>(m_ptr[0]) == 0xEF &&
            static_cast<unsigned char>(m_ptr[1]) == 0xBB &&
            static_cast<unsigned char>(m_ptr[2]) == 0xBF) {
            m_ptr += 3;
        }
        SkipWhitespace();
    }

    bool ParseValue(Value& out, int depth) {
        if (depth > 64) return Fail("JSON nesting too deep");
        SkipWhitespace();
        if (m_ptr >= m_end) return Fail("Unexpected end of JSON");

        switch (*m_ptr) {
        case '{': return ParseObject(out, depth);
        case '[': return ParseArray(out, depth);
        case '"':
            out.type = Value::Type::String;
            return ParseString(out.string);
        case 't':
            if (ConsumeLiteral("true")) { out.type = Value::Type::Bool; out.boolean = true; return true; }
            return Fail("Invalid literal");
        case 'f':
            if (ConsumeLiteral("false")) { out.type = Value::Type::Bool; out.boolean = false; return true; }
            return Fail("Invalid literal");
        case 'n':
            if (ConsumeLiteral("null")) { out.type = Value::Type::Null; return true; }
            return Fail("Invalid literal");
        default:
            return ParseNumber(out);
        }
    }

    bool ConsumeLiteral(const char* literal) {
        const std::size_t length = std::char_traits<char>::length(literal);
        if (static_cast<std::size_t>(m_end - m_ptr) < length) return false;
        if (std::string(m_ptr, length) != literal) return false;
        m_ptr += length;
        return true;
    }

    bool ParseObject(Value& out, int depth) {
        out.type = Value::Type::Object;
        ++m_ptr; // '{'
        SkipWhitespace();
        if (m_ptr < m_end && *m_ptr == '}') { ++m_ptr; return true; }

        while (m_ptr < m_end) {
            SkipWhitespace();
            if (m_ptr >= m_end || *m_ptr != '"') return Fail("Expected object key string");

            std::string key;
            if (!ParseString(key)) return false;
            SkipWhitespace();
            if (m_ptr >= m_end || *m_ptr != ':') return Fail("Expected ':' after object key");
            ++m_ptr;

            Value child;
            if (!ParseValue(child, depth + 1)) return false;
            out.object[key] = std::move(child);

            SkipWhitespace();
            if (m_ptr >= m_end) return Fail("Unterminated object");
            if (*m_ptr == ',') { ++m_ptr; continue; }
            if (*m_ptr == '}') { ++m_ptr; return true; }
            return Fail("Expected ',' or '}' in object");
        }
        return Fail("Unterminated object");
    }

    bool ParseArray(Value& out, int depth) {
        out.type = Value::Type::Array;
        ++m_ptr; // '['
        SkipWhitespace();
        if (m_ptr < m_end && *m_ptr == ']') { ++m_ptr; return true; }

        while (m_ptr < m_end) {
            Value child;
            if (!ParseValue(child, depth + 1)) return false;
            out.array.push_back(std::move(child));

            SkipWhitespace();
            if (m_ptr >= m_end) return Fail("Unterminated array");
            if (*m_ptr == ',') { ++m_ptr; continue; }
            if (*m_ptr == ']') { ++m_ptr; return true; }
            return Fail("Expected ',' or ']' in array");
        }
        return Fail("Unterminated array");
    }

    static void AppendUtf8(std::string& out, unsigned int codepoint) {
        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    bool ParseHex4(unsigned int& value) {
        if (m_end - m_ptr < 4) return Fail("Invalid unicode escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = *m_ptr++;
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else return Fail("Invalid unicode escape");
        }
        return true;
    }

    bool ParseString(std::string& out) {
        out.clear();
        if (m_ptr >= m_end || *m_ptr != '"') return Fail("Expected string");
        ++m_ptr;

        while (m_ptr < m_end) {
            const unsigned char c = static_cast<unsigned char>(*m_ptr++);
            if (c == '"') return true;
            if (c == '\\') {
                if (m_ptr >= m_end) return Fail("Unterminated escape");
                const char esc = *m_ptr++;
                switch (esc) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    unsigned int codepoint = 0;
                    if (!ParseHex4(codepoint)) return false;
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && m_end - m_ptr >= 6 &&
                        m_ptr[0] == '\\' && m_ptr[1] == 'u') {
                        m_ptr += 2;
                        unsigned int low = 0;
                        if (!ParseHex4(low)) return false;
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                        }
                    }
                    AppendUtf8(out, codepoint);
                    break;
                }
                default:
                    return Fail("Invalid escape sequence");
                }
            } else if (c < 0x20) {
                return Fail("Control character inside string");
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
        return Fail("Unterminated string");
    }

    bool ParseNumber(Value& out) {
        const char* start = m_ptr;
        if (m_ptr < m_end && (*m_ptr == '-' || *m_ptr == '+')) ++m_ptr;
        while (m_ptr < m_end && *m_ptr >= '0' && *m_ptr <= '9') ++m_ptr;
        if (m_ptr < m_end && *m_ptr == '.') {
            ++m_ptr;
            while (m_ptr < m_end && *m_ptr >= '0' && *m_ptr <= '9') ++m_ptr;
        }
        if (m_ptr < m_end && (*m_ptr == 'e' || *m_ptr == 'E')) {
            ++m_ptr;
            if (m_ptr < m_end && (*m_ptr == '-' || *m_ptr == '+')) ++m_ptr;
            while (m_ptr < m_end && *m_ptr >= '0' && *m_ptr <= '9') ++m_ptr;
        }
        if (start == m_ptr) return Fail("Invalid value");
        try {
            out.type = Value::Type::Number;
            out.number = std::stod(std::string(start, m_ptr));
        } catch (...) {
            return Fail("Invalid number");
        }
        return true;
    }
};

} // namespace detail

inline bool Parse(const std::string& text, Value& out, std::string* error = nullptr) {
    detail::Parser parser(text.data(), text.size());
    std::string localError;
    if (!parser.ParseDocument(out, localError)) {
        if (error) *error = localError;
        return false;
    }
    return true;
}

} // namespace json
