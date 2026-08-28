#if defined(PLATFORM_RG_NANO) || defined(RG_NANO_ASSET_GATE_TEST)

#include "platform/sha1.h"

#include <string.h>

static uint32_t RotateLeft(uint32_t value, unsigned int shift)
{
    return (value << shift) | (value >> (32 - shift));
}

static void Transform(struct Sha1Context *context, const uint8_t block[64])
{
    uint32_t words[80];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    int i;

    for (i = 0; i < 16; i++)
    {
        words[i] = ((uint32_t)block[i * 4] << 24)
                 | ((uint32_t)block[i * 4 + 1] << 16)
                 | ((uint32_t)block[i * 4 + 2] << 8)
                 | block[i * 4 + 3];
    }
    for (i = 16; i < 80; i++)
        words[i] = RotateLeft(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    for (i = 0; i < 80; i++)
    {
        uint32_t function;
        uint32_t constant;
        uint32_t temporary;
        if (i < 20)
        {
            function = (b & c) | ((~b) & d);
            constant = 0x5A827999;
        }
        else if (i < 40)
        {
            function = b ^ c ^ d;
            constant = 0x6ED9EBA1;
        }
        else if (i < 60)
        {
            function = (b & c) | (b & d) | (c & d);
            constant = 0x8F1BBCDC;
        }
        else
        {
            function = b ^ c ^ d;
            constant = 0xCA62C1D6;
        }
        temporary = RotateLeft(a, 5) + function + e + constant + words[i];
        e = d;
        d = c;
        c = RotateLeft(b, 30);
        b = a;
        a = temporary;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
}

void Sha1_Init(struct Sha1Context *context)
{
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
    context->length = 0;
    context->blockLength = 0;
}

void Sha1_Update(struct Sha1Context *context, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    context->length += (uint64_t)length * 8;
    while (length > 0)
    {
        size_t space = sizeof(context->block) - context->blockLength;
        size_t take = length < space ? length : space;
        memcpy(context->block + context->blockLength, bytes, take);
        context->blockLength += take;
        bytes += take;
        length -= take;
        if (context->blockLength == sizeof(context->block))
        {
            Transform(context, context->block);
            context->blockLength = 0;
        }
    }
}

void Sha1_Final(struct Sha1Context *context, uint8_t digest[20])
{
    uint8_t lengthBytes[8];
    uint8_t padding[64] = {0x80};
    size_t paddingLength;
    int i;
    uint64_t bitLength = context->length;

    for (i = 0; i < 8; i++)
        lengthBytes[7 - i] = (uint8_t)(bitLength >> (i * 8));
    paddingLength = context->blockLength < 56 ? 56 - context->blockLength
                                             : 120 - context->blockLength;
    Sha1_Update(context, padding, paddingLength);
    Sha1_Update(context, lengthBytes, sizeof(lengthBytes));
    for (i = 0; i < 5; i++)
    {
        digest[i * 4] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)context->state[i];
    }
    memset(context, 0, sizeof(*context));
}

#endif
