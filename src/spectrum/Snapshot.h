#pragma once
#include <stdint.h>
#include <stddef.h>

class SpectrumBase;

class Snapshot {
public:
    /**
     * @brief Load a .z80 snapshot from the given filepath and apply it to the spectrum.
     * 
     * @param spectrum The spectrum hardware instance.
     * @param filepath Path to the .z80 file.
     * @return true if loaded successfully.
     */
    static bool load(SpectrumBase* spectrum, const char* filepath);

    /**
     * @brief Check if /spiffs/autoexec.z80 exists and load it if it does.
     * 
     * @param spectrum The spectrum hardware instance.
     * @return true if an autoexec was found and loaded successfully.
     */
    static bool loadAutoexec(SpectrumBase* spectrum);

private:
    struct Z80SnapshotHeader {
        uint8_t hdr[30];
        uint16_t extra_len;
        uint16_t ext_pc;
        bool haveHeader;
    };

    static void restoreCPUFromSnapshot(SpectrumBase* spectrum, const Z80SnapshotHeader& header, const uint8_t* filebuf, size_t got);
    static bool decompressZ80RLE(const uint8_t* src, size_t srclen, uint8_t* dest, size_t destlen, size_t offset);
    static void loadCompressedMemPage(const uint8_t* src, size_t srclen, uint8_t* memPage, size_t memlen);
};
