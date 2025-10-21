#include "ResourceTypes.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <cstring>
#include <lz4.h>
#include <filesystem>
#include <functional>

using namespace std;

struct FileInfo {
    string filename;
    uint64_t id;
    time_t mtime;
    vector<char> data;
    vector<char> compressedData;
    uint32_t compressionType;
};

bool loadFile(const string& filename, vector<char>& data, time_t& mtime) {
    struct stat st;
    if (stat(filename.c_str(), &st) != 0) return false;
    mtime = st.st_mtime;
    ifstream file(filename, ios::binary | ios::ate);
    if (!file) return false;
    size_t size = file.tellg();
    file.seekg(0);
    data.resize(size);
    file.read(data.data(), size);
    return true;
}

void compressData(const vector<char>& input, vector<char>& output, uint32_t& compressionType) {
    int maxCompressedSize = LZ4_compressBound(input.size());
    output.resize(maxCompressedSize);
    int compressedSize = LZ4_compress_default(input.data(), output.data(), input.size(), maxCompressedSize);
    if (compressedSize > 0 && compressedSize < (int)input.size()) {
        output.resize(compressedSize);
        compressionType = COMPRESSION_FLAGS_LZ4;
    } else {
        // Compression didn't help, store uncompressed
        output = input;
        compressionType = COMPRESSION_FLAGS_UNCOMPRESSED;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Usage: packer <output.pak> <file1> <file2> ..." << endl;
        return 1;
    }

    string output = argv[1];
    vector<FileInfo> files;
    for (int i = 2; i < argc; ++i) {
        string filename = argv[i];
        string basename = filesystem::path(filename).filename().string();
        uint64_t id = hash<string>{}(basename);
        files.push_back({filename, id});
        cout << "Adding file: " << filename << " with ID " << id << endl;
    }

    bool needRebuild = true;
    // Check if pak exists and timestamps match
    ifstream pakFile(output, ios::binary);
    if (pakFile) {
        PakFileHeader header;
        pakFile.read((char*)&header, sizeof(header));
        if (pakFile && memcmp(header.sig, "PAKC", 4) == 0 && header.numResources == (uint32_t)files.size()) {
            vector<ResourcePtr> ptrs(header.numResources);
            pakFile.read((char*)ptrs.data(), sizeof(ResourcePtr) * header.numResources);
            needRebuild = false;
            for (auto& file : files) {
                if (!loadFile(file.filename, file.data, file.mtime)) {
                    needRebuild = true;
                    break;
                }
                // Find the ptr
                bool found = false;
                for (auto& ptr : ptrs) {
                    if (ptr.id == file.id) {
                        if (ptr.lastModified != (uint64_t)file.mtime) {
                            needRebuild = true;
                        }
                        found = true;
                        break;
                    }
                }
                if (!found || needRebuild) {
                    needRebuild = true;
                    break;
                }
            }
        } else {
            needRebuild = true;
        }
    } else {
        needRebuild = true;
    }

    if (!needRebuild) {
        cout << "Pak file is up to date" << endl;
        return 0;
    }

    // Rebuild
    cout << "Building pak file" << endl;
    for (auto& file : files) {
        if (!loadFile(file.filename, file.data, file.mtime)) {
            cerr << "Failed to load " << file.filename << endl;
            return 1;
        }
        compressData(file.data, file.compressedData, file.compressionType);
        cout << "File " << file.filename << " original " << file.data.size() << " compressed " << file.compressedData.size() << " type " << file.compressionType << endl;
    }

    ofstream out(output, ios::binary);
    PakFileHeader header;
    memcpy(header.sig, "PAKC", 4);
    header.version = VERSION_1_0;
    header.numResources = files.size();
    header.pad = 0;
    out.write((char*)&header, sizeof(header));

    uint64_t offset = sizeof(header) + sizeof(ResourcePtr) * files.size();
    for (auto& file : files) {
        ResourcePtr ptr = {file.id, offset, (uint64_t)file.mtime};
        out.write((char*)&ptr, sizeof(ptr));
        offset += sizeof(CompressionHeader) + file.compressedData.size();
    }

    for (auto& file : files) {
        CompressionHeader comp = {file.compressionType, (uint32_t)file.compressedData.size(), (uint32_t)file.data.size(), RESOURCE_TYPE_SHADER};
        out.write((char*)&comp, sizeof(comp));
        out.write(file.compressedData.data(), file.compressedData.size());
    }

    cout << "Pak file created" << endl;
    return 0;
}