#include "Models/Formats/FbxParser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <zlib.h>

namespace Models::FbxParser {
namespace {

constexpr std::uint8_t kMagic[23] = {
    'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a','r','y',' ',' ',0,0x1a,0
};

struct Reader {
    const std::uint8_t *data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    std::string *error = nullptr;

    bool fail(const std::string& message)
    {
        if (error && error->empty()) *error = message;
        return false;
    }

    bool need(std::size_t bytes)
    {
        return bytes <= size - std::min(pos, size) || fail("unexpected end of FBX data");
    }

    template <typename T>
    bool read(T *out)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!out || !need(sizeof(T))) return false;
        std::memcpy(out, data + pos, sizeof(T));
        pos += sizeof(T);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        std::reverse(
            reinterpret_cast<std::uint8_t *>(out),
            reinterpret_cast<std::uint8_t *>(out) + sizeof(T)
        );
#endif
        return true;
    }

    bool readBytes(std::size_t bytes, const std::uint8_t **out)
    {
        if (!out || !need(bytes)) return false;
        *out = data + pos;
        pos += bytes;
        return true;
    }

    bool skip(std::size_t bytes)
    {
        if (!need(bytes)) return false;
        pos += bytes;
        return true;
    }
};

bool isZeroRecord(const Reader& reader, std::size_t bytes)
{
    if (reader.pos + bytes > reader.size) return false;
    for (std::size_t i = 0; i < bytes; ++i) {
        if (reader.data[reader.pos + i] != 0u) return false;
    }
    return true;
}

template <typename T>
bool decodeArray(
    Reader& reader,
    std::uint32_t count,
    std::uint32_t encoding,
    std::uint32_t stored_bytes,
    std::vector<T> *out)
{
    if (!out) return reader.fail("null FBX array output");
    if (count > std::numeric_limits<std::uint32_t>::max() / std::max<std::size_t>(sizeof(T), 1u)) {
        return reader.fail("FBX array size overflow");
    }

    const std::size_t expected = static_cast<std::size_t>(count) * sizeof(T);
    const std::uint8_t *payload = nullptr;
    if (!reader.readBytes(stored_bytes, &payload)) return false;

    std::vector<std::uint8_t> raw;
    if (encoding == 0u) {
        if (stored_bytes < expected) return reader.fail("short uncompressed FBX array");
        raw.assign(payload, payload + expected);
    } else if (encoding == 1u) {
        raw.resize(expected);
        uLongf output_size = static_cast<uLongf>(expected);
        const int status = uncompress(
            reinterpret_cast<Bytef *>(raw.data()),
            &output_size,
            reinterpret_cast<const Bytef *>(payload),
            static_cast<uLong>(stored_bytes)
        );
        if (status != Z_OK || output_size != expected) {
            return reader.fail("failed to inflate FBX array");
        }
    } else {
        return reader.fail("unsupported FBX array encoding");
    }

    out->resize(count);
    if (expected != 0u) std::memcpy(out->data(), raw.data(), expected);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (T& value : *out) {
        std::reverse(
            reinterpret_cast<std::uint8_t *>(&value),
            reinterpret_cast<std::uint8_t *>(&value) + sizeof(T)
        );
    }
#endif
    return true;
}

bool readProperty(Reader& reader, Property *out)
{
    if (!out) return reader.fail("null FBX property output");
    std::uint8_t code = 0;
    if (!reader.read(&code)) return false;
    out->type = static_cast<char>(code);

    switch (out->type) {
        case 'Y': {
            std::int16_t value = 0;
            if (!reader.read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'C': {
            std::uint8_t value = 0;
            if (!reader.read(&value)) return false;
            out->value = (value == 'T') || (value != 'F' && ((value & 1u) != 0u));
            return true;
        }
        case 'I': {
            std::int32_t value = 0;
            if (!reader.read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'F': {
            float value = 0.0f;
            if (!reader.read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'D': {
            double value = 0.0;
            if (!reader.read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'L': {
            std::int64_t value = 0;
            if (!reader.read(&value)) return false;
            out->value = value;
            return true;
        }
        case 'S':
        case 'R': {
            std::uint32_t length = 0;
            if (!reader.read(&length)) return false;
            const std::uint8_t *bytes = nullptr;
            if (!reader.readBytes(length, &bytes)) return false;
            if (out->type == 'S') {
                out->value = std::string(reinterpret_cast<const char *>(bytes), length);
            } else {
                out->value = Bytes(bytes, bytes + length);
            }
            return true;
        }
        case 'f':
        case 'd':
        case 'i':
        case 'l':
        case 'b': {
            std::uint32_t count = 0;
            std::uint32_t encoding = 0;
            std::uint32_t stored = 0;
            if (!reader.read(&count) || !reader.read(&encoding) || !reader.read(&stored)) return false;
            if (out->type == 'f') {
                std::vector<float> array;
                if (!decodeArray(reader, count, encoding, stored, &array)) return false;
                out->value = std::move(array);
                return true;
            }
            if (out->type == 'd') {
                std::vector<double> array;
                if (!decodeArray(reader, count, encoding, stored, &array)) return false;
                out->value = std::move(array);
                return true;
            }
            if (out->type == 'i') {
                std::vector<std::int32_t> array;
                if (!decodeArray(reader, count, encoding, stored, &array)) return false;
                out->value = std::move(array);
                return true;
            }
            if (out->type == 'l') {
                std::vector<std::int64_t> array;
                if (!decodeArray(reader, count, encoding, stored, &array)) return false;
                out->value = std::move(array);
                return true;
            }
            std::vector<std::uint8_t> array;
            if (!decodeArray(reader, count, encoding, stored, &array)) return false;
            out->value = Bytes(array.begin(), array.end());
            return true;
        }
        default:
            return reader.fail(std::string("unsupported FBX property type: ") + out->type);
    }
}

bool readNode(Reader& reader, std::uint32_t version, Node *out, bool *was_null)
{
    if (!out || !was_null) return reader.fail("invalid FBX node output");
    *was_null = false;

    const bool wide = version >= 7500u;
    const std::size_t sentinel_size = wide ? 25u : 13u;
    if (isZeroRecord(reader, sentinel_size)) {
        reader.pos += sentinel_size;
        *was_null = true;
        return true;
    }

    std::uint64_t end_offset = 0;
    std::uint64_t property_count = 0;
    std::uint64_t property_bytes = 0;
    if (wide) {
        if (!reader.read(&end_offset) || !reader.read(&property_count) || !reader.read(&property_bytes)) {
            return false;
        }
    } else {
        std::uint32_t end32 = 0;
        std::uint32_t count32 = 0;
        std::uint32_t bytes32 = 0;
        if (!reader.read(&end32) || !reader.read(&count32) || !reader.read(&bytes32)) return false;
        end_offset = end32;
        property_count = count32;
        property_bytes = bytes32;
    }

    std::uint8_t name_length = 0;
    if (!reader.read(&name_length)) return false;
    if (end_offset == 0u) {
        *was_null = true;
        return true;
    }
    if (end_offset > reader.size || end_offset < reader.pos) {
        return reader.fail("invalid FBX node end offset");
    }

    const std::uint8_t *name_bytes = nullptr;
    if (!reader.readBytes(name_length, &name_bytes)) return false;
    out->name.assign(reinterpret_cast<const char *>(name_bytes), name_length);

    const std::size_t properties_begin = reader.pos;
    if (property_count > 10000000ull) return reader.fail("unreasonable FBX property count");
    out->properties.reserve(static_cast<std::size_t>(property_count));
    for (std::uint64_t i = 0; i < property_count; ++i) {
        Property property;
        if (!readProperty(reader, &property)) return false;
        out->properties.push_back(std::move(property));
    }

    if (reader.pos < properties_begin || reader.pos - properties_begin > property_bytes) {
        return reader.fail("FBX property list length mismatch");
    }
    if (reader.pos - properties_begin < property_bytes) {
        if (!reader.skip(static_cast<std::size_t>(property_bytes - (reader.pos - properties_begin)))) {
            return false;
        }
    }

    while (reader.pos < end_offset) {
        if (isZeroRecord(reader, sentinel_size)) {
            reader.pos += sentinel_size;
            break;
        }
        Node child;
        bool null_child = false;
        if (!readNode(reader, version, &child, &null_child)) return false;
        if (null_child) break;
        out->children.push_back(std::move(child));
    }

    if (reader.pos > end_offset) return reader.fail("FBX node exceeded declared end offset");
    if (reader.pos < end_offset) reader.pos = static_cast<std::size_t>(end_offset);
    return true;
}

bool parseBinary(
    const std::uint8_t *data,
    std::size_t size,
    Document *out,
    std::string *error)
{
    if (size < 27u) {
        if (error) *error = "FBX binary header is truncated";
        return false;
    }
    if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) {
        if (error) *error = "invalid FBX binary magic";
        return false;
    }

    Reader reader{data, size, 23u, error};
    std::uint32_t version = 0;
    if (!reader.read(&version)) return false;
    out->version = version;
    out->binary = true;
    out->root.name.clear();

    const std::size_t sentinel_size = version >= 7500u ? 25u : 13u;
    while (reader.pos + sentinel_size <= size) {
        if (isZeroRecord(reader, sentinel_size)) break;
        Node node;
        bool was_null = false;
        if (!readNode(reader, version, &node, &was_null)) return false;
        if (was_null) break;
        out->root.children.push_back(std::move(node));
    }
    return true;
}

struct Token {
    enum class Type {
        Identifier,
        String,
        Number,
        Colon,
        Comma,
        LBrace,
        RBrace,
        Star,
        End,
    } type = Type::End;
    std::string text;
};

class Lexer {
public:
    Lexer(const std::uint8_t *data, std::size_t size)
        : text_(reinterpret_cast<const char *>(data), size)
    {
    }

    Token next()
    {
        skipSpace();
        if (pos_ >= text_.size()) return {Token::Type::End, {}};

        const char c = text_[pos_++];
        if (c == ':') return {Token::Type::Colon, ":"};
        if (c == ',') return {Token::Type::Comma, ","};
        if (c == '{') return {Token::Type::LBrace, "{"};
        if (c == '}') return {Token::Type::RBrace, "}"};
        if (c == '*') return {Token::Type::Star, "*"};
        if (c == '"') {
            std::string value;
            bool escape = false;
            while (pos_ < text_.size()) {
                const char ch = text_[pos_++];
                if (escape) {
                    value.push_back(ch);
                    escape = false;
                    continue;
                }
                if (ch == '\\') {
                    escape = true;
                    continue;
                }
                if (ch == '"') break;
                value.push_back(ch);
            }
            return {Token::Type::String, std::move(value)};
        }

        const bool numeric =
            std::isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.';
        std::string value(1, c);
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (
                std::isspace(static_cast<unsigned char>(ch)) ||
                ch == ':' || ch == ',' || ch == '{' || ch == '}' || ch == '*' || ch == ';')
            {
                break;
            }
            value.push_back(ch);
            ++pos_;
        }
        return {numeric ? Token::Type::Number : Token::Type::Identifier, std::move(value)};
    }

private:
    void skipSpace()
    {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos_;
                continue;
            }
            if (c == ';') {
                while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }

    std::string text_;
    std::size_t pos_ = 0u;
};

class AsciiParser {
public:
    AsciiParser(const std::uint8_t *data, std::size_t size, std::string *error)
        : lexer_(data, size), error_(error)
    {
        advance();
    }

    bool parse(Document *out)
    {
        out->binary = false;
        out->version = 7400u;
        out->root = {};
        while (current_.type != Token::Type::End) {
            Node node;
            if (!parseNode(&node)) return false;
            out->root.children.push_back(std::move(node));
        }
        return true;
    }

private:
    void advance() { current_ = lexer_.next(); }

    bool fail(const std::string& message)
    {
        if (error_ && error_->empty()) *error_ = message;
        return false;
    }

    bool parseNode(Node *node)
    {
        if (current_.type != Token::Type::Identifier) {
            return fail("expected FBX ASCII node name");
        }
        node->name = current_.text;
        advance();
        if (current_.type != Token::Type::Colon) {
            return fail("expected ':' after FBX ASCII node name");
        }
        advance();

        if (current_.type == Token::Type::Star) {
            advance();
            if (current_.type == Token::Type::Number) advance();
            if (current_.type != Token::Type::LBrace) {
                return fail("expected '{' after FBX ASCII array size");
            }
            advance();
            if (current_.type == Token::Type::Identifier && current_.text == "a") {
                advance();
                if (current_.type == Token::Type::Colon) advance();
            }

            std::vector<double> array;
            while (current_.type != Token::Type::RBrace && current_.type != Token::Type::End) {
                if (current_.type == Token::Type::Comma) {
                    advance();
                    continue;
                }
                if (current_.type != Token::Type::Number) {
                    return fail("expected number in FBX ASCII array");
                }
                char *end = nullptr;
                const double value = std::strtod(current_.text.c_str(), &end);
                if (!end || *end != '\0') return fail("invalid FBX ASCII number");
                array.push_back(value);
                advance();
            }
            if (current_.type == Token::Type::RBrace) advance();
            node->properties.push_back(Property{'d', std::move(array)});
            return true;
        }

        while (
            current_.type != Token::Type::LBrace &&
            current_.type != Token::Type::RBrace &&
            current_.type != Token::Type::End)
        {
            if (current_.type == Token::Type::Comma) {
                advance();
                continue;
            }

            Property property;
            if (current_.type == Token::Type::String) {
                property.type = 'S';
                property.value = current_.text;
                advance();
            } else if (current_.type == Token::Type::Number) {
                if (current_.text.find_first_of(".eE") != std::string::npos) {
                    property.type = 'D';
                    property.value = std::strtod(current_.text.c_str(), nullptr);
                } else {
                    property.type = 'L';
                    property.value = static_cast<std::int64_t>(
                        std::strtoll(current_.text.c_str(), nullptr, 10)
                    );
                }
                advance();
            } else if (current_.type == Token::Type::Identifier) {
                property.type = 'S';
                property.value = current_.text;
                advance();
            } else {
                return fail("invalid FBX ASCII property");
            }
            node->properties.push_back(std::move(property));
        }

        if (current_.type == Token::Type::LBrace) {
            advance();
            while (current_.type != Token::Type::RBrace && current_.type != Token::Type::End) {
                Node child;
                if (!parseNode(&child)) return false;
                node->children.push_back(std::move(child));
            }
            if (current_.type != Token::Type::RBrace) {
                return fail("unterminated FBX ASCII node");
            }
            advance();
        }
        return true;
    }

    Lexer lexer_;
    Token current_;
    std::string *error_ = nullptr;
};

} // namespace

std::int64_t Property::asInt64(std::int64_t fallback) const
{
    return std::visit(
        [fallback](const auto& value) -> std::int64_t {
            using T = std::decay_t<decltype(value)>;
            if constexpr (
                std::is_same_v<T, std::int16_t> ||
                std::is_same_v<T, std::int32_t> ||
                std::is_same_v<T, std::int64_t>)
            {
                return static_cast<std::int64_t>(value);
            } else if constexpr (std::is_same_v<T, bool>) {
                return value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
                return static_cast<std::int64_t>(value);
            } else {
                return fallback;
            }
        },
        value
    );
}

double Property::asDouble(double fallback) const
{
    return std::visit(
        [fallback](const auto& value) -> double {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_arithmetic_v<T>) {
                return static_cast<double>(value);
            } else {
                return fallback;
            }
        },
        value
    );
}

std::string Property::asString(std::string fallback) const
{
    if (const auto *value_string = std::get_if<std::string>(&value)) return *value_string;
    return fallback;
}

const std::vector<double> *Property::asDoubleArray() const
{
    return std::get_if<std::vector<double>>(&value);
}

const std::vector<float> *Property::asFloatArray() const
{
    return std::get_if<std::vector<float>>(&value);
}

const std::vector<std::int32_t> *Property::asInt32Array() const
{
    return std::get_if<std::vector<std::int32_t>>(&value);
}

const std::vector<std::int64_t> *Property::asInt64Array() const
{
    return std::get_if<std::vector<std::int64_t>>(&value);
}

const Node *Node::child(const std::string& child_name) const
{
    for (const Node& node : children) {
        if (node.name == child_name) return &node;
    }
    return nullptr;
}

std::vector<const Node *> Node::childrenNamed(const std::string& child_name) const
{
    std::vector<const Node *> result;
    for (const Node& node : children) {
        if (node.name == child_name) result.push_back(&node);
    }
    return result;
}

std::vector<double> Node::numericArray() const
{
    if (properties.empty()) return {};
    if (const auto *array = properties[0].asDoubleArray()) return *array;
    if (const auto *array = properties[0].asFloatArray()) {
        return std::vector<double>(array->begin(), array->end());
    }
    if (const auto *array = properties[0].asInt32Array()) {
        return std::vector<double>(array->begin(), array->end());
    }
    if (const auto *array = properties[0].asInt64Array()) {
        return std::vector<double>(array->begin(), array->end());
    }

    std::vector<double> result;
    result.reserve(properties.size());
    for (const Property& property : properties) result.push_back(property.asDouble());
    return result;
}

bool parseMemory(
    const std::uint8_t *data,
    std::size_t size,
    Document *out,
    std::string *error)
{
    if (error) error->clear();
    if (!data || !out) {
        if (error) *error = "invalid FBX input";
        return false;
    }

    *out = {};
    if (size >= sizeof(kMagic) && std::memcmp(data, kMagic, sizeof(kMagic)) == 0) {
        return parseBinary(data, size, out, error);
    }

    AsciiParser parser(data, size, error);
    return parser.parse(out);
}

bool parseFile(const std::string& path, Document *out, std::string *error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        if (error) *error = "failed to open FBX file: " + path;
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff length = file.tellg();
    if (length < 0) {
        if (error) *error = "failed to size FBX file";
        return false;
    }
    file.seekg(0, std::ios::beg);

    Bytes bytes(static_cast<std::size_t>(length));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    if (!file && !bytes.empty()) {
        if (error) *error = "failed to read FBX file";
        return false;
    }
    return parseMemory(bytes.data(), bytes.size(), out, error);
}

} // namespace Models::FbxParser
