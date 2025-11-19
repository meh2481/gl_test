#include "../src/ResourceTypes.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <sys/stat.h>
#include <cstring>
#include <lz4.h>
#include <filesystem>
#include <functional>
#include <png.h>
#include <squish.h>
#include <cassert>
#include <opusfile.h>

using namespace std;

struct FileInfo {
    string filename;
    uint64_t id;
    time_t mtime;
    vector<char> data;
    vector<char> compressedData;
    uint32_t compressionType;
    bool changed;
    uint64_t offset; // for unchanged files
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

uint32_t getFileType(const string& filename) {
    string ext = filesystem::path(filename).extension().string();
    if (ext == ".lua") return RESOURCE_TYPE_LUA;
    if (ext == ".spv") return RESOURCE_TYPE_SHADER;
    if (ext == ".png") return RESOURCE_TYPE_IMAGE;
    if (ext == ".opus") return RESOURCE_TYPE_SOUND;
    // Add more extensions as needed
    return RESOURCE_TYPE_UNKNOWN;
}

// Load PNG image and convert to RGBA
bool loadPNG(const string& filename, vector<uint8_t>& imageData, uint32_t& width, uint32_t& height, bool& hasAlpha) {
    FILE* fp = fopen(filename.c_str(), "rb");
    if (!fp) return false;
    
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }
    
    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(fp);
        return false;
    }
    
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }
    
    png_init_io(png, fp);
    png_read_info(png, info);
    
    width = png_get_image_width(png, info);
    height = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);
    
    // Error out if image is paletted or grayscale
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        cerr << "Error: Paletted PNG images are not supported: " << filename << endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }
    
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        cerr << "Error: Grayscale PNG images are not supported: " << filename << endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }
    
    // Only support RGB and RGBA
    if (color_type != PNG_COLOR_TYPE_RGB && color_type != PNG_COLOR_TYPE_RGBA) {
        cerr << "Error: Unsupported PNG color type: " << filename << endl;
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(fp);
        return false;
    }
    
    // Strip 16-bit to 8-bit
    if (bit_depth == 16)
        png_set_strip_16(png);
    
    png_read_update_info(png, info);
    
    // Determine if the image has alpha
    hasAlpha = (color_type == PNG_COLOR_TYPE_RGBA);
    
    // Allocate memory for image data
    int bytesPerPixel = hasAlpha ? 4 : 3;
    imageData.resize(width * height * bytesPerPixel);
    
    vector<png_bytep> row_pointers(height);
    for (uint32_t y = 0; y < height; y++) {
        row_pointers[y] = imageData.data() + y * width * bytesPerPixel;
    }
    
    png_read_image(png, row_pointers.data());
    png_read_end(png, nullptr);
    
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(fp);
    
    return true;
}

// Compress image data using DXT/BC compression
void compressImage(const vector<uint8_t>& imageData, vector<char>& compressed, uint32_t width, uint32_t height, bool hasAlpha, uint16_t& format) {
    assert(width > 0 && height > 0);
    int bytesPerPixel = hasAlpha ? 4 : 3;
    assert(imageData.size() == width * height * bytesPerPixel);
    
    // Choose compression format based on alpha channel
    int flags;
    if (hasAlpha) {
        flags = squish::kDxt5;  // BC3/DXT5 for RGBA
        format = IMAGE_FORMAT_BC3_DXT5;
    } else {
        flags = squish::kDxt1;  // BC1/DXT1 for RGB
        format = IMAGE_FORMAT_BC1_DXT1;
    }
    
    // Calculate storage requirements
    int storageSize = squish::GetStorageRequirements(width, height, flags);
    assert(storageSize > 0);
    compressed.resize(storageSize);
    
    // For RGB images, we need to convert to RGBA for squish
    if (!hasAlpha) {
        // Create temporary RGBA buffer with full alpha
        vector<uint8_t> rgbaData(width * height * 4);
        for (uint32_t i = 0; i < width * height; i++) {
            rgbaData[i * 4 + 0] = imageData[i * 3 + 0];  // R
            rgbaData[i * 4 + 1] = imageData[i * 3 + 1];  // G
            rgbaData[i * 4 + 2] = imageData[i * 3 + 2];  // B
            rgbaData[i * 4 + 3] = 255;                   // A (fully opaque)
        }
        squish::CompressImage(rgbaData.data(), width, height, compressed.data(), flags);
    } else {
        // Compress RGBA directly
        squish::CompressImage(imageData.data(), width, height, compressed.data(), flags);
    }
}

// Process PNG file: load, compress, and prepend ImageHeader
bool processPNGFile(const string& filename, vector<char>& output) {
    vector<uint8_t> imageData;
    uint32_t width, height;
    bool hasAlpha;
    
    if (!loadPNG(filename, imageData, width, height, hasAlpha)) {
        return false;
    }
    
    assert(width > 0 && height > 0);
    int bytesPerPixel = hasAlpha ? 4 : 3;
    assert(imageData.size() == width * height * bytesPerPixel);
    
    // Compress the image data
    vector<char> compressedImage;
    uint16_t format;
    compressImage(imageData, compressedImage, width, height, hasAlpha, format);
    
    assert(compressedImage.size() > 0);
    
    // Create ImageHeader
    ImageHeader header;
    header.format = format;
    header.width = width;
    header.height = height;
    header.pad = 0;
    
    // Combine header and compressed data
    output.resize(sizeof(ImageHeader) + compressedImage.size());
    memcpy(output.data(), &header, sizeof(ImageHeader));
    memcpy(output.data() + sizeof(ImageHeader), compressedImage.data(), compressedImage.size());
    
    return true;
}

// Process OPUS file: load, decode to PCM, and store as raw audio
bool processOPUSFile(const string& filename, vector<char>& output) {
    int error = 0;
    OggOpusFile* opusFile = op_open_file(filename.c_str(), &error);
    if (!opusFile) {
        cerr << "Failed to open OPUS file " << filename << " (error: " << error << ")" << endl;
        return false;
    }
    
    // Get channel count
    int channels = op_channel_count(opusFile, -1);
    if (channels <= 0 || channels > 2) {
        cerr << "Unsupported channel count in OPUS file: " << channels << endl;
        op_free(opusFile);
        return false;
    }
    
    // Get total PCM length
    ogg_int64_t totalSamples = op_pcm_total(opusFile, -1);
    if (totalSamples < 0) {
        cerr << "Failed to get PCM length from OPUS file" << endl;
        op_free(opusFile);
        return false;
    }
    
    // Allocate buffer for decoded PCM data (16-bit samples)
    size_t totalSize = totalSamples * channels * sizeof(int16_t);
    vector<int16_t> pcmData(totalSamples * channels);
    
    // Decode the entire file
    size_t samplesRead = 0;
    while (samplesRead < (size_t)totalSamples) {
        int samplesToRead = totalSamples - samplesRead;
        if (samplesToRead > 5760) samplesToRead = 5760; // opus recommends reading in chunks
        
        int ret = op_read(opusFile, pcmData.data() + samplesRead * channels, samplesToRead * channels, nullptr);
        if (ret < 0) {
            cerr << "Error decoding OPUS file: " << ret << endl;
            op_free(opusFile);
            return false;
        }
        if (ret == 0) break; // End of file
        samplesRead += ret;
    }
    
    op_free(opusFile);
    
    // OPUS always decodes to 48kHz
    int sampleRate = 48000;
    int bitsPerSample = 16;
    
    // Create output with raw PCM data
    // We'll store: sampleRate(4) + channels(4) + bitsPerSample(4) + PCM data
    output.resize(12 + samplesRead * channels * sizeof(int16_t));
    memcpy(output.data(), &sampleRate, 4);
    memcpy(output.data() + 4, &channels, 4);
    memcpy(output.data() + 8, &bitsPerSample, 4);
    memcpy(output.data() + 12, pcmData.data(), samplesRead * channels * sizeof(int16_t));
    
    cout << "Decoded OPUS: " << samplesRead << " samples, " << channels << " channels, " 
         << sampleRate << " Hz, " << bitsPerSample << " bits" << endl;
    
    return true;
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

    vector<ResourcePtr> existingPtrs;
    // Check if pak exists and timestamps match
    ifstream pakFile(output, ios::binary);
    if (pakFile) {
        PakFileHeader header;
        pakFile.read((char*)&header, sizeof(header));
        if (pakFile && memcmp(header.sig, "PAKC", 4) == 0 && header.numResources == (uint32_t)files.size()) {
            existingPtrs.resize(header.numResources);
            pakFile.read((char*)existingPtrs.data(), sizeof(ResourcePtr) * header.numResources);
            for (auto& file : files) {
                if (!loadFile(file.filename, file.data, file.mtime)) {
                    file.changed = true;
                    continue;
                }
                // Find the ptr
                bool found = false;
                for (auto& ptr : existingPtrs) {
                    if (ptr.id == file.id) {
                        if (ptr.lastModified != (uint64_t)file.mtime) {
                            file.changed = true;
                        } else {
                            file.changed = false;
                            file.offset = ptr.offset;
                        }
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    file.changed = true;
                }
            }
        } else {
            for (auto& file : files) {
                file.changed = true;
            }
        }
    } else {
        for (auto& file : files) {
            file.changed = true;
        }
    }

    // Check if any file changed
    bool anyChanged = false;
    for (const auto& file : files) {
        if (file.changed) {
            anyChanged = true;
            break;
        }
    }

    if (!anyChanged) {
        cout << "Pak file is up to date" << endl;
        return 0;
    }

    // Rebuild
    cout << "Building pak file" << endl;
    for (auto& file : files) {
        if (file.changed) {
            uint32_t fileType = getFileType(file.filename);
            
            // Special handling for PNG images
            if (fileType == RESOURCE_TYPE_IMAGE) {
                if (!processPNGFile(file.filename, file.data)) {
                    cerr << "Failed to process PNG file " << file.filename << endl;
                    return 1;
                }
                // Get modification time
                struct stat st;
                if (stat(file.filename.c_str(), &st) == 0) {
                    file.mtime = st.st_mtime;
                }
                // PNG data is already compressed with DXT, apply LZ4 on top
                compressData(file.data, file.compressedData, file.compressionType);
                cout << "File " << file.filename << " original " << file.data.size() << " compressed " << file.compressedData.size() << " compression " << file.compressionType << " resource_type " << fileType << endl;
            } else if (fileType == RESOURCE_TYPE_SOUND) {
                // Special handling for OPUS audio files
                if (!processOPUSFile(file.filename, file.data)) {
                    cerr << "Failed to process OPUS file " << file.filename << endl;
                    return 1;
                }
                // Get modification time
                struct stat st;
                if (stat(file.filename.c_str(), &st) == 0) {
                    file.mtime = st.st_mtime;
                }
                // OPUS data is decoded to PCM, apply LZ4 compression
                compressData(file.data, file.compressedData, file.compressionType);
                cout << "File " << file.filename << " original " << file.data.size() << " compressed " << file.compressedData.size() << " compression " << file.compressionType << " resource_type " << fileType << endl;
            } else {
                // Standard file processing
                if (!loadFile(file.filename, file.data, file.mtime)) {
                    cerr << "Failed to load " << file.filename << endl;
                    return 1;
                }
                compressData(file.data, file.compressedData, file.compressionType);
                cout << "File " << file.filename << " original " << file.data.size() << " compressed " << file.compressedData.size() << " compression " << file.compressionType << " resource_type " << fileType << endl;
            }
        } else {
            // Load from existing pak
            pakFile.seekg(file.offset);
            CompressionHeader comp;
            pakFile.read((char*)&comp, sizeof(comp));
            file.compressedData.resize(comp.compressedSize);
            pakFile.read(file.compressedData.data(), comp.compressedSize);
            file.compressionType = comp.compressionType;
            file.data.resize(comp.decompressedSize); // Not needed, but for consistency
            cout << "File " << file.filename << " unchanged, using cached data" << endl;
        }
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
        CompressionHeader comp = {file.compressionType, (uint32_t)file.compressedData.size(), (uint32_t)file.data.size(), getFileType(file.filename)};
        out.write((char*)&comp, sizeof(comp));
        out.write(file.compressedData.data(), file.compressedData.size());
    }

    cout << "Pak file created" << endl;
    return 0;
}