#ifndef GUARD_PLATFORM_SHA1_H
#define GUARD_PLATFORM_SHA1_H

#include <stddef.h>
#include <stdint.h>

struct Sha1Context
{
    uint32_t state[5];
    uint64_t length;
    uint8_t block[64];
    size_t blockLength;
};

void Sha1_Init(struct Sha1Context *context);
void Sha1_Update(struct Sha1Context *context, const void *data, size_t length);
void Sha1_Final(struct Sha1Context *context, uint8_t digest[20]);

#endif
