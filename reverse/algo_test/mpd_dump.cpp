// Minimal read-only Melodyne GNBCFA graph inspector.
// Build: g++ -O2 -std=c++17 -o mpd_dump mpd_dump.cpp -lz
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>
#include <zlib.h>

using Bytes = std::vector<std::uint8_t>;

template <typename T>
std::optional<T> readLittle(const Bytes& data, std::size_t offset) {
    if (offset > data.size() || sizeof(T) > data.size() - offset) return std::nullopt;
    T value{};
    std::memcpy(&value, data.data() + offset, sizeof(T));
    return value;
}

Bytes readFile(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (!file) return {};
    std::fseek(file, 0, SEEK_END);
    const auto size = std::ftell(file);
    std::rewind(file);
    Bytes data(size > 0 ? static_cast<std::size_t>(size) : 0);
    if (!data.empty() && std::fread(data.data(), 1, data.size(), file) != data.size()) data.clear();
    std::fclose(file);
    return data;
}

Bytes inflateZlib(const std::uint8_t* input, std::size_t size) {
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input);
    stream.avail_in = static_cast<uInt>(size);
    if (inflateInit(&stream) != Z_OK) return {};
    Bytes output;
    std::uint8_t block[65536];
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = block;
        stream.avail_out = sizeof(block);
        status = inflate(&stream, Z_NO_FLUSH);
        output.insert(output.end(), block, block + sizeof(block) - stream.avail_out);
    }
    inflateEnd(&stream);
    return status == Z_STREAM_END ? output : Bytes{};
}

Bytes decodeGraph(const Bytes& file) {
    if (file.size() < 8 || std::memcmp(file.data(), "GNBCFA", 6) != 0) return {};
    std::size_t offset = 8;
    while (offset + 8 <= file.size()) {
        const auto nameLength = readLittle<std::uint32_t>(file, offset).value_or(0);
        offset += 8;
        if (offset + nameLength > file.size()) return {};
        std::string name(reinterpret_cast<const char*>(file.data() + offset), nameLength);
        offset = (offset + nameLength + 7u) & ~std::size_t(7u);
        const auto storedLength = readLittle<std::uint64_t>(file, offset).value_or(0);
        offset += 8;
        if (storedLength > file.size() - offset) return {};
        if (name.find("melodyne.graph") != std::string::npos) {
            if (storedLength >= 20 && std::memcmp(file.data() + offset, "GNBKVAi\0", 8) == 0) {
                const auto compressed = readLittle<std::uint32_t>(file, offset + 16).value_or(0);
                if (compressed != storedLength - 20) return {};
                return inflateZlib(file.data() + offset + 20, compressed);
            }
            return Bytes(file.begin() + offset, file.begin() + offset + storedLength);
        }
        offset = (offset + static_cast<std::size_t>(storedLength) + 7u) & ~std::size_t(7u);
    }
    return {};
}

std::optional<std::size_t> valueSize(std::uint32_t type) {
    switch (type) {
        case 0x85e: case 0x864: case 0x86c: case 0x871: return 8;
        case 0x162: case 0x163: return 1;
        case 0x469: case 0x466: return 4;
        case 0x1052: return 16;
        default: return std::nullopt;
    }
}

struct Field { std::string name; std::uint32_t type = 0; };
struct Class { std::string name; std::vector<Field> fields; };
struct Record { std::size_t start = 0, end = 0; bool present = false; };

class Graph {
public:
    bool parse(Bytes input) {
        data = std::move(input);
        std::size_t offset = 0;
        auto take = [&]() -> std::optional<std::uint32_t> {
            auto value = readLittle<std::uint32_t>(data, offset);
            if (value) offset += 4;
            return value;
        };
        const auto keyCount = take();
        if (!keyCount || *keyCount > 16384) return false;
        std::printf("keys=%u ", *keyCount);
        std::vector<std::string> keys;
        for (std::uint32_t i = 0; i < *keyCount; ++i) {
            const auto length = take();
            if (!length || offset + *length > data.size()) return false;
            keys.emplace_back(reinterpret_cast<const char*>(data.data() + offset), *length);
            offset += *length;
        }
        const auto classCount = take();
        if (!classCount || *classCount > 2048) return false;
        std::printf("classes=%u ", *classCount);
        for (std::uint32_t i = 0; i < *classCount; ++i) {
            const auto length = take();
            if (!length || offset + *length > data.size()) return false;
            Class schema;
            schema.name.assign(reinterpret_cast<const char*>(data.data() + offset), *length);
            offset += *length;
            if (!take()) return false;
            const auto fieldCount = take();
            if (!fieldCount) return false;
            for (std::uint32_t field = 0; field < *fieldCount; ++field) {
                const auto key = take(), multiplicity = take(), type = take();
                if (!key || !multiplicity || !type || *key >= keys.size() || !valueSize(*type)
                    || offset >= data.size()) return false;
                ++offset;
                schema.fields.push_back({keys[*key], *type});
            }
            classes.push_back(std::move(schema));
        }
        const auto objectCount = take();
        if (!objectCount || *objectCount > 2000000) return false;
        std::printf("objects=%u ", *objectCount);
        objectClasses.resize(*objectCount);
        for (std::size_t index = 0; index < objectClasses.size(); ++index) {
            const auto value = take();
            if (!value || (*value >= classes.size() && *value != 0xffffffffu)) {
                std::printf("object_class_error index=%zu value=%u offset=%zu\n",
                            index, value.value_or(0), offset);
                return false;
            }
            objectClasses[index] = *value;
        }
        const auto serializedCount = take();
        if (!serializedCount || *serializedCount > *objectCount) return false;
        std::printf("serialized=%u records_at=%zu\n", *serializedCount, offset);
        std::vector<std::uint32_t> ids(*serializedCount);
        for (auto& id : ids) {
            const auto value = take();
            if (!value || *value >= *objectCount) return false;
            id = *value;
        }
        records.resize(*objectCount);
        for (auto id : ids) {
            const auto start = offset;
            const auto length = take();
            if (!length || offset + *length > data.size()) {
                std::printf("record_error id=%u offset=%zu length=%u size=%zu\n",
                            id, offset, length.value_or(0), data.size());
                return false;
            }
            records[id] = {start, offset + *length, true};
            offset += *length;
        }
        return true;
    }

    void dump() const {
        for (std::size_t id = 0; id < records.size(); ++id) {
            if (!records[id].present) continue;
            if (objectClasses[id] == 0xffffffffu) continue;
            const auto& schema = classes[objectClasses[id]];
            const auto relevant = (id >= 315 && id <= 331) || id == 356
                || schema.name == "MULSSGenerator"
                || schema.name == "MUAudioSourceDescription"
                || schema.name == "MUSpectrumShaperParameterSet"
                || schema.name == "MUSpectrumShaperSpectrum"
                || schema.name == "MUSpectrumShaperEnvelope"
                || schema.name == "MUAudioSourcePrincipalItem"
                || schema.name == "MUAudioSourceComponent"
                || schema.name == "MUDecomposedAudioSignal"
                || schema.name == "MUSampledFunction"
                || std::any_of(schema.fields.begin(), schema.fields.end(),
                    [](const Field& field) {
                        return field.name == "pitchCenter" || field.name == "targetPitchCenter"
                            || field.name == "formantOffset";
                    });
            if (!relevant) continue;
            std::printf("object=%zu class=%s\n", id, schema.name.c_str());
            std::size_t offset = records[id].start + 8;
            for (const auto& field : schema.fields) {
                const auto size = *valueSize(field.type);
                const auto lower = lowercase(field.name);
                if ((id >= 315 && id <= 331) || id == 356
                    || schema.name == "MULSSGenerator"
                    || schema.name == "MUAudioSourceDescription"
                    || schema.name == "MUSpectrumShaperParameterSet"
                    || schema.name == "MUSpectrumShaperSpectrum"
                    || schema.name == "MUSpectrumShaperEnvelope"
                    || schema.name == "MUAudioSourcePrincipalItem"
                    || schema.name == "MUAudioSourceComponent"
                    || schema.name == "MUDecomposedAudioSignal"
                    || schema.name == "MUSampledFunction"
                    || lower.find("pitch") != std::string::npos
                    || lower.find("formant") != std::string::npos
                    || lower.find("attack") != std::string::npos
                    || lower == "starttime" || lower == "duration")
                    printValue(field, offset);
                offset += size;
            }
            if (schema.name == "GNData" && offset < records[id].end) {
                const auto bytes = records[id].end - offset;
                std::printf("  rawBytes=%zu", bytes);
                const auto count = std::min<std::size_t>(bytes / sizeof(float), 8);
                for (std::size_t index = 0; index < count; ++index)
                    std::printf(" %.7g", readLittle<float>(data, offset + index * sizeof(float)).value_or(0.0f));
                std::printf("\n");
            }
        }
    }

private:
    static std::string lowercase(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    void printValue(const Field& field, std::size_t offset) const {
        if (field.type == 0x85e)
            std::printf("  %s=ref:%u\n", field.name.c_str(),
                        readLittle<std::uint32_t>(data, offset).value_or(0xffffffffu));
        else if (field.type == 0x162 || field.type == 0x163)
            std::printf("  %s=%s\n", field.name.c_str(), data[offset] ? "true" : "false");
        else if (field.type == 0x466)
            std::printf("  %s=%.9g\n", field.name.c_str(), readLittle<float>(data, offset).value_or(0));
        else if (field.type == 0x864)
            std::printf("  %s=%.12g\n", field.name.c_str(), readLittle<double>(data, offset).value_or(0));
        else if (field.type == 0x469)
            std::printf("  %s=%d\n", field.name.c_str(), readLittle<std::int32_t>(data, offset).value_or(0));
        else if (field.type == 0x86c)
            std::printf("  %s=%lld\n", field.name.c_str(),
                        static_cast<long long>(readLittle<std::int64_t>(data, offset).value_or(0)));
        else if (field.type == 0x871) {
            const auto numerator = readLittle<std::int32_t>(data, offset).value_or(0);
            const auto denominator = readLittle<std::int32_t>(data, offset + 4).value_or(0);
            std::printf("  %s=%.12g\n", field.name.c_str(), denominator ? double(numerator) / denominator : 0.0);
        }
    }

    Bytes data;
    std::vector<Class> classes;
    std::vector<std::uint32_t> objectClasses;
    std::vector<Record> records;
};

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) { std::printf("use: %s project.mpd [decoded.bin]\n", argv[0]); return 1; }
    auto graphBytes = decodeGraph(readFile(argv[1]));
    if (graphBytes.empty()) {
        std::printf("cannot decode graph in %s\n", argv[1]);
        return 1;
    }
    std::printf("decoded_graph_bytes=%zu\n", graphBytes.size());
    if (argc == 3) {
        FILE* output = std::fopen(argv[2], "wb");
        if (!output || std::fwrite(graphBytes.data(), 1, graphBytes.size(), output) != graphBytes.size()) {
            if (output) std::fclose(output);
            std::printf("cannot write %s\n", argv[2]);
            return 1;
        }
        std::fclose(output);
    }
    Graph graph;
    if (!graph.parse(std::move(graphBytes))) {
        std::printf("cannot parse %s\n", argv[1]);
        return 1;
    }
    graph.dump();
    return 0;
}
