#ifdef PLATFORM_RG_NANO

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform/rg_nano_asset_gate.h"
#include "platform/sha1.h"

#include <dlfcn.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define BASEROM_SIZE (16 * 1024 * 1024)
#define MANIFEST_HEADER_SIZE 44
#define MANIFEST_ENTRY_SIZE 12
#define MAX_MANIFEST_ENTRIES 131072

static const uint8_t sExpectedRomSha1[20] = {
    0xF3, 0xAE, 0x08, 0x81, 0x81, 0xBF, 0x58, 0x3E, 0x55, 0xDA,
    0xF9, 0x62, 0xA9, 0x2B, 0xB4, 0x6F, 0x4F, 0x1D, 0x07, 0xB7,
};

struct ExecutableImage
{
    uintptr_t base;
    const ElfW(Phdr) *headers;
    ElfW(Half) headerCount;
    uint8_t buildId[20];
    int found;
};

static uint32_t ReadLe32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0]
         | ((uint32_t)bytes[1] << 8)
         | ((uint32_t)bytes[2] << 16)
         | ((uint32_t)bytes[3] << 24);
}

static int FindExecutable(struct dl_phdr_info *info, size_t size, void *data)
{
    struct ExecutableImage *image = data;
    // Identify our own image by finding the object whose PT_LOAD ranges contain
    // a symbol we know lives in it. Selecting "the object with an empty
    // dlpi_name" is a glibc-ism: musl reports the main executable by its full
    // path, so the only unnamed object here is the vDSO -- which carries its own
    // GNU build-id note, so the gate read the kernel's build id and reported
    // BUILD ID MISMATCH on every launch.
    uintptr_t self = (uintptr_t)(void *)&RgNanoAssetGate_Fill;
    int contains = 0;
    int i;
    (void)size;

    for (i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *header = &info->dlpi_phdr[i];
        uintptr_t start;
        uintptr_t end;
        if (header->p_type != PT_LOAD)
            continue;
        start = info->dlpi_addr + header->p_vaddr;
        end = start + header->p_memsz;
        if (self >= start && self < end)
        {
            contains = 1;
            break;
        }
    }
    if (!contains)
        return 0;

    image->base = info->dlpi_addr;
    image->headers = info->dlpi_phdr;
    image->headerCount = info->dlpi_phnum;
    for (i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *header = &info->dlpi_phdr[i];
        const uint8_t *cursor;
        const uint8_t *end;
        if (header->p_type != PT_NOTE)
            continue;
        cursor = (const uint8_t *)(info->dlpi_addr + header->p_vaddr);
        end = cursor + header->p_memsz;
        while (cursor + sizeof(ElfW(Nhdr)) <= end)
        {
            const ElfW(Nhdr) *note = (const ElfW(Nhdr) *)cursor;
            const uint8_t *name = cursor + sizeof(*note);
            const uint8_t *description = name + ((note->n_namesz + 3) & ~3);
            const uint8_t *next = description + ((note->n_descsz + 3) & ~3);
            if (next > end)
                break;
            if (note->n_type == NT_GNU_BUILD_ID && note->n_namesz == 4
             && note->n_descsz == sizeof(image->buildId) && memcmp(name, "GNU", 4) == 0)
            {
                memcpy(image->buildId, description, sizeof(image->buildId));
                image->found = 1;
            }
            cursor = next;
        }
    }
    return 1;
}

static const ElfW(Phdr) *FindLoadSegment(const struct ExecutableImage *image,
                                         uintptr_t address,
                                         size_t length)
{
    int i;
    for (i = 0; i < image->headerCount; i++)
    {
        const ElfW(Phdr) *header = &image->headers[i];
        uintptr_t start;
        uintptr_t end;
        if (header->p_type != PT_LOAD)
            continue;
        start = image->base + header->p_vaddr;
        end = start + header->p_memsz;
        if (address >= start && length <= end - address)
            return header;
    }
    return NULL;
}

static int SegmentProtection(const ElfW(Phdr) *header)
{
    int protection = 0;
    if (header->p_flags & PF_R) protection |= PROT_READ;
    if (header->p_flags & PF_W) protection |= PROT_WRITE;
    if (header->p_flags & PF_X) protection |= PROT_EXEC;
    return protection;
}

static void SetDetail(char *detail, size_t size, const char *format, const char *value)
{
    if (detail != NULL && size > 0)
        snprintf(detail, size, format, value == NULL ? "" : value);
}

const char *RgNanoAssetGate_ResultText(int result)
{
    switch (result)
    {
    case RG_NANO_ASSET_GATE_OK: return "ROM ASSETS READY";
    case RG_NANO_ASSET_GATE_DEVELOPMENT_BUILD: return "DEVELOPMENT BUILD";
    case RG_NANO_ASSET_GATE_MANIFEST_INVALID: return "ASSET MANIFEST INVALID";
    case RG_NANO_ASSET_GATE_ROM_MISSING: return "BASEROM.GBA MISSING";
    case RG_NANO_ASSET_GATE_ROM_SIZE: return "ROM MUST BE 16 MIB";
    case RG_NANO_ASSET_GATE_ROM_HASH: return "WRONG EMERALD ROM";
    case RG_NANO_ASSET_GATE_BUILD_ID: return "BUILD ID MISMATCH";
    case RG_NANO_ASSET_GATE_MEMORY: return "ASSET FILL FAILED";
    default: return "UNKNOWN ASSET ERROR";
    }
}

int RgNanoAssetGate_Fill(const char *dataDirectory,
                         const char *manifestPath,
                         char *detail,
                         size_t detailSize)
{
    struct ExecutableImage image = {0};
    struct Sha1Context sha1;
    struct stat romStat;
    uint8_t manifestHeader[MANIFEST_HEADER_SIZE];
    uint8_t digest[20];
    char romPath[1024];
    FILE *manifest;
    int romFd = -1;
    uint8_t *rom = MAP_FAILED;
    uint32_t entryCount;
    uint32_t entryIndex;
    long pageSize;
    int result = RG_NANO_ASSET_GATE_MEMORY;

    if (detail != NULL && detailSize > 0)
        detail[0] = '\0';
    manifest = fopen(manifestPath, "rb");
    if (manifest == NULL)
    {
        if (errno == ENOENT)
            return RG_NANO_ASSET_GATE_DEVELOPMENT_BUILD;
        SetDetail(detail, detailSize, "CANNOT OPEN %s", manifestPath);
        return RG_NANO_ASSET_GATE_MANIFEST_INVALID;
    }
    if (fread(manifestHeader, 1, sizeof(manifestHeader), manifest) != sizeof(manifestHeader))
    {
        result = RG_NANO_ASSET_GATE_MANIFEST_INVALID;
        goto done;
    }
    entryCount = ReadLe32(manifestHeader + 40);
    if (entryCount == 0 || entryCount > MAX_MANIFEST_ENTRIES
     || memcmp(manifestHeader, sExpectedRomSha1, sizeof(sExpectedRomSha1)) != 0)
    {
        result = RG_NANO_ASSET_GATE_MANIFEST_INVALID;
        goto done;
    }

    dl_iterate_phdr(FindExecutable, &image);
    if (!image.found || memcmp(image.buildId, manifestHeader + 20, sizeof(image.buildId)) != 0)
    {
        result = RG_NANO_ASSET_GATE_BUILD_ID;
        goto done;
    }

    if (snprintf(romPath, sizeof(romPath), "%s/baserom.gba", dataDirectory) >= (int)sizeof(romPath))
    {
        result = RG_NANO_ASSET_GATE_ROM_MISSING;
        goto done;
    }
    romFd = open(romPath, O_RDONLY);
    if (romFd < 0)
    {
        SetDetail(detail, detailSize, "COPY ROM TO %s", romPath);
        result = RG_NANO_ASSET_GATE_ROM_MISSING;
        goto done;
    }
    if (fstat(romFd, &romStat) != 0 || romStat.st_size != BASEROM_SIZE)
    {
        result = RG_NANO_ASSET_GATE_ROM_SIZE;
        goto done;
    }
    rom = mmap(NULL, BASEROM_SIZE, PROT_READ, MAP_PRIVATE, romFd, 0);
    if (rom == MAP_FAILED)
    {
        result = RG_NANO_ASSET_GATE_MEMORY;
        goto done;
    }
    Sha1_Init(&sha1);
    Sha1_Update(&sha1, rom, BASEROM_SIZE);
    Sha1_Final(&sha1, digest);
    if (memcmp(digest, sExpectedRomSha1, sizeof(digest)) != 0
     || memcmp(digest, manifestHeader, sizeof(digest)) != 0)
    {
        result = RG_NANO_ASSET_GATE_ROM_HASH;
        goto done;
    }

    pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
    {
        result = RG_NANO_ASSET_GATE_MEMORY;
        goto done;
    }
    for (entryIndex = 0; entryIndex < entryCount; entryIndex++)
    {
        uint8_t entry[MANIFEST_ENTRY_SIZE];
        uint32_t virtualAddress;
        uint32_t length;
        uint32_t romOffset;
        uintptr_t destination;
        uintptr_t pageStart;
        uintptr_t pageEnd;
        const ElfW(Phdr) *segment;
        int protection;
        if (fread(entry, 1, sizeof(entry), manifest) != sizeof(entry))
        {
            result = RG_NANO_ASSET_GATE_MANIFEST_INVALID;
            goto done;
        }
        virtualAddress = ReadLe32(entry);
        length = ReadLe32(entry + 4);
        romOffset = ReadLe32(entry + 8);
        if (length == 0 || romOffset > BASEROM_SIZE || length > BASEROM_SIZE - romOffset)
        {
            result = RG_NANO_ASSET_GATE_MANIFEST_INVALID;
            goto done;
        }
        destination = image.base + virtualAddress;
        segment = FindLoadSegment(&image, destination, length);
        if (segment == NULL)
        {
            result = RG_NANO_ASSET_GATE_MANIFEST_INVALID;
            goto done;
        }
        protection = SegmentProtection(segment);
        pageStart = destination & ~((uintptr_t)pageSize - 1);
        pageEnd = (destination + length + pageSize - 1) & ~((uintptr_t)pageSize - 1);
        if (mprotect((void *)pageStart, pageEnd - pageStart, protection | PROT_WRITE) != 0)
        {
            result = RG_NANO_ASSET_GATE_MEMORY;
            goto done;
        }
        memcpy((void *)destination, rom + romOffset, length);
        if (protection & PROT_EXEC)
            __builtin___clear_cache((char *)destination, (char *)destination + length);
        if (mprotect((void *)pageStart, pageEnd - pageStart, protection) != 0)
        {
            result = RG_NANO_ASSET_GATE_MEMORY;
            goto done;
        }
    }
    if (fgetc(manifest) != EOF)
    {
        result = RG_NANO_ASSET_GATE_MANIFEST_INVALID;
        goto done;
    }
    result = RG_NANO_ASSET_GATE_OK;

done:
    if (rom != MAP_FAILED)
        munmap(rom, BASEROM_SIZE);
    if (romFd >= 0)
        close(romFd);
    fclose(manifest);
    // Deliberately leave detail empty when there is nothing more to say: the
    // caller already shows RgNanoAssetGate_ResultText as the headline, and
    // echoing it here printed the same error twice on the gate screen.
    return result;
}

#endif
