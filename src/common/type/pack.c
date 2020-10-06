/***********************************************************************************************************************************
Pack Type

Each pack field begins with a one byte tag. The four high order bits of the tag contain the field type (PackType). The four lower
order bits vary by type.

Integer types (packTypeData[type].valueMultiBit) when the value is >= -1 and <= 1:
  3 more value indicator set to 0
  2 value low-order bit
  1 more ID delta indicator
  0 ID delta low order bit

Integer types (packTypeData[type].valueMultiBit) when the value is < -1 or > 1:
  3 more value indicator set to 1
  2 more ID delta indicator
0-1 ID delta low order bit

String and binary types (packTypeData[type].valueSingleBit):
  3 value bit
  2 more ID delta indicator
0-1 ID delta low order bits

Array and object types:
  4 more ID delta indicator
0-3 ID delta low order bit

When the "more ID delta" indicator is set then the tag will be followed by a base-128 encoded integer with the higher order ID delta
bits. The ID delta represents the delta from the ID of the previous field. When the "more value indicator" then the tag (and the ID
delta, if any) will be followed by a base-128 encoded integer with the high order value bits, i.e. the bits that were not stored
directly in the tag.

For integer types the value is the integer being stored. For string and binary types the value is 1 if the size is greater than 0
and 0 if the size is 0. When the size is greater than 0 the tag is immediately followed the base-128 encoded size and then by the
string/binary bytes.
***********************************************************************************************************************************/
#include "build.auto.h"

#include <string.h>

#include "common/debug.h"
#include "common/io/bufferRead.h"
#include "common/io/bufferWrite.h"
#include "common/io/io.h"
#include "common/io/read.h"
#include "common/io/write.h"
#include "common/log.h" // !!! REMOVE
#include "common/type/convert.h"
#include "common/type/object.h"
#include "common/type/pack.h"

/***********************************************************************************************************************************
Constants
***********************************************************************************************************************************/
#define PACK_UINT64_SIZE_MAX                                        10

/***********************************************************************************************************************************
Type data
***********************************************************************************************************************************/
typedef struct PackTypeData
{
    PackType type;
    bool valueSingleBit;
    bool valueMultiBit;
    bool size;
    const String *const name;
} PackTypeData;

static const PackTypeData packTypeData[] =
{
    {
        .type = pckTypeUnknown,
        .name = STRDEF("unknown"),
    },
    {
        .type = pckTypeArray,
        .name = STRDEF("array"),
    },
    {
        .type = pckTypeBin,
        .valueSingleBit = true,
        .size = true,
        .name = STRDEF("bin"),
    },
    {
        .type = pckTypeBool,
        .valueSingleBit = true,
        .name = STRDEF("bool"),
    },
    {
        .type = pckTypeI32,
        .valueMultiBit = true,
        .name = STRDEF("i32"),
    },
    {
        .type = pckTypeI64,
        .valueMultiBit = true,
        .name = STRDEF("i64"),
    },
    {
        .type = pckTypeObj,
        .name = STRDEF("obj"),
    },
    {
        .type = pckTypePtr,
        .valueMultiBit = true,
        .name = STRDEF("ptr"),
    },
    {
        .type = pckTypeStr,
        .valueSingleBit = true,
        .size = true,
        .name = STRDEF("str"),
    },
    {
        .type = pckTypeTime,
        .valueMultiBit = true,
        .name = STRDEF("time"),
    },
    {
        .type = pckTypeU32,
        .valueMultiBit = true,
        .name = STRDEF("u32"),
    },
    {
        .type = pckTypeU64,
        .valueMultiBit = true,
        .name = STRDEF("u64"),
    },
};

/***********************************************************************************************************************************
Object type
***********************************************************************************************************************************/
typedef struct PackTagStack
{
    PackType type;
    unsigned int idLast;
    unsigned int nullTotal;
} PackTagStack;

struct PackRead
{
    MemContext *memContext;                                         // Mem context
    IoRead *read;                                                   // Read pack from
    Buffer *buffer;                                                 // Buffer to contain read data
    const uint8_t *bufferPtr;                                       // Pointer to buffer
    size_t bufferPos;                                               // Position in the buffer
    size_t bufferMax;                                               // Maximum position of buffer

    unsigned int tagNextId;                                         // Next tag id
    PackType tagNextType;                                           // Next tag type
    uint64_t tagNextValue;                                          // Next tag value

    List *tagStack;                                                 // Stack of object/array tags
    PackTagStack *tagStackTop;                                      // Top tag on the stack
};

OBJECT_DEFINE_FREE(PACK_READ);

struct PackWrite
{
    MemContext *memContext;                                         // Mem context
    IoWrite *write;                                                 // Write pack to
    Buffer *buffer;                                                 // Buffer to contain write data

    List *tagStack;                                                 // Stack of object/array tags
    PackTagStack *tagStackTop;                                      // Top tag on the stack
};

OBJECT_DEFINE_FREE(PACK_WRITE);

/**********************************************************************************************************************************/
static PackRead *
pckReadNewInternal(void)
{
    FUNCTION_TEST_VOID();

    PackRead *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("PackRead")
    {
        this = memNew(sizeof(PackRead));

        *this = (PackRead)
        {
            .memContext = MEM_CONTEXT_NEW(),
            .tagStack = lstNewP(sizeof(PackTagStack)),
        };

        this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

PackRead *
pckReadNew(IoRead *read)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_READ, read);
    FUNCTION_TEST_END();

    ASSERT(read != NULL);

    PackRead *this = pckReadNewInternal();
    this->read = read;
    this->buffer = bufNew(ioBufferSize());
    this->bufferPtr = bufPtr(this->buffer);

    FUNCTION_TEST_RETURN(this);
}

PackRead *
pckReadNewBuf(const Buffer *buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(buffer != NULL);

    PackRead *this = pckReadNewInternal();
    this->bufferPtr = bufPtrConst(buffer);
    this->bufferMax = bufUsed(buffer);

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
!!!
DON"T USE THIS FUNCTION AS A PARAMETER IN FUNCTION CALLS SINCE this->bufferPos MAY BE UPDATED.
***********************************************************************************************************************************/
static size_t pckReadBuffer(PackRead *this, size_t size)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(SIZE, size);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    size_t remaining = this->bufferMax - this->bufferPos;

    if (remaining < size)
    {
        if (this->read != NULL)
        {
            // Nothing can be remaining since each read fetches exactly the number of bytes required
            ASSERT(remaining == 0);
            bufUsedZero(this->buffer);

            // Limit the buffer for the next read so we don't read past the end of the pack
            bufLimitSet(this->buffer, size < bufSizeAlloc(this->buffer) ? size : bufSizeAlloc(this->buffer));

            // Read bytes
            ioReadSmall(this->read, this->buffer);
            this->bufferPos = 0;
            this->bufferMax = bufUsed(this->buffer);
            remaining = this->bufferMax - this->bufferPos;
        }

        if (remaining < 1)
            THROW(FormatError, "unexpected EOF");

        // !!! ASSERT((size < this->bufferMax - this->bufferPos) && size == bufSize);
        FUNCTION_TEST_RETURN(remaining < size ? remaining : size);
    }

    FUNCTION_TEST_RETURN(size);
}

/***********************************************************************************************************************************
Unpack an unsigned 64-bit integer from base-128 varint encoding
***********************************************************************************************************************************/
static uint64_t
pckReadUInt64Internal(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    uint64_t result = 0;
    uint8_t byte;

    for (unsigned int bufferIdx = 0; bufferIdx < PACK_UINT64_SIZE_MAX; bufferIdx++)
    {
        pckReadBuffer(this, 1);
        byte = this->bufferPtr[this->bufferPos];

        result |= (uint64_t)(byte & 0x7f) << (7 * bufferIdx);

        if (byte < 0x80)
            break;

        this->bufferPos++;
    }

    if (byte >= 0x80)
        THROW(FormatError, "unterminated base-128 integer");

    this->bufferPos++;

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
!!!
***********************************************************************************************************************************/
static bool
pckReadTagNext(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    bool result = 0;

    pckReadBuffer(this, 1);
    unsigned int tag = this->bufferPtr[this->bufferPos];
    this->bufferPos++;

    if (tag == 0)
        this->tagNextId = 0xFFFFFFFF;
    else
    {
        this->tagNextType = tag >> 4;

        if (packTypeData[this->tagNextType].valueMultiBit)
        {
            if (tag & 0x8)
            {
                this->tagNextId = tag & 0x3;

                if (tag & 0x4)
                    this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 2;

                this->tagNextValue = pckReadUInt64Internal(this);
            }
            else
            {
                this->tagNextId = tag & 0x1;

                if (tag & 0x2)
                    this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 1;

                this->tagNextValue = (tag >> 2) & 0x1;
            }
        }
        else if (packTypeData[this->tagNextType].valueSingleBit)
        {
            this->tagNextId = tag & 0x3;

            if (tag & 0x4)
                this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 2;

            this->tagNextValue = (tag >> 3) & 0x1;
        }
        else
        {
            this->tagNextId = tag & 0x7;

            if (tag & 0x8)
                this->tagNextId |= (unsigned int)pckReadUInt64Internal(this) << 3;

            this->tagNextValue = 0;
        }

        this->tagNextId += this->tagStackTop->idLast + 1;
        result = true;
    }

    FUNCTION_TEST_RETURN(result);
}

/***********************************************************************************************************************************
!!!
***********************************************************************************************************************************/
static uint64_t
pckReadTag(PackRead *this, unsigned int *id, PackType type, bool peek)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM_P(UINT, id);
        FUNCTION_TEST_PARAM(ENUM, type);
        FUNCTION_TEST_PARAM(BOOL, peek);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(id != NULL);
    ASSERT((peek && type == pckTypeUnknown) || (!peek && type != pckTypeUnknown));

    if (*id == 0)
        *id = this->tagStackTop->idLast + 1;
    else if (*id <= this->tagStackTop->idLast)
        THROW_FMT(FormatError, "field %u was already read", *id);

    do
    {
        if (this->tagNextId == 0)
            pckReadTagNext(this);

        if (*id < this->tagNextId)
        {
            if (!peek)
                THROW_FMT(FormatError, "field %u does not exist", *id);

            break;
        }
        else if (*id == this->tagNextId)
        {
            if (!peek)
            {
                if (this->tagNextType != type)
                {
                    THROW_FMT(
                        FormatError, "field %u is type '%s' but expected '%s'", this->tagNextId,
                        strZ(packTypeData[this->tagNextType].name), strZ(packTypeData[type].name));
                }

                this->tagStackTop->idLast = this->tagNextId;
                this->tagNextId = 0;
            }

            break;
        }

        // Read data for the field being skipped
        if (packTypeData[type].size && this->tagNextValue != 0)
        {
            size_t sizeExpected = (size_t)pckReadUInt64Internal(this);

            while (sizeExpected != 0)
            {
                size_t sizeRead = pckReadBuffer(this, sizeExpected);
                sizeExpected -= sizeRead;
                this->bufferPos += sizeRead;
            }
        }

        this->tagStackTop->idLast = this->tagNextId;
        this->tagNextId = 0;
    }
    while (1);

    FUNCTION_TEST_RETURN(this->tagNextValue);
}

/**********************************************************************************************************************************/
bool
pckReadNext(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(pckReadTagNext(this));
}

/**********************************************************************************************************************************/
unsigned int
pckReadId(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->tagNextId);
}

/**********************************************************************************************************************************/
// Helper to !!!
static inline bool
pckReadNullInternal(PackRead *this, unsigned int *id)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM_P(UINT, id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(id != NULL);

    pckReadTag(this, id, pckTypeUnknown, true);

    FUNCTION_TEST_RETURN(*id < this->tagNextId);
}

bool
pckReadNull(PackRead *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(pckReadNullInternal(this, &param.id));
}

/***********************************************************************************************************************************
!!!
***********************************************************************************************************************************/
static inline bool
pckReadDefaultNull(PackRead *this, bool defaultNull, unsigned int *id)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(BOOL, defaultNull);
        FUNCTION_TEST_PARAM_P(UINT, id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(id != NULL);

    if (defaultNull && pckReadNullInternal(this, id))
    {
        this->tagStackTop->idLast = *id;
        FUNCTION_TEST_RETURN(true);
    }

    FUNCTION_TEST_RETURN(false);
}

/**********************************************************************************************************************************/
PackType
pckReadType(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    FUNCTION_TEST_RETURN(this->tagNextType);
}

/**********************************************************************************************************************************/
void
pckReadArrayBegin(PackRead *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    pckReadTag(this, &param.id, pckTypeArray, false);
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeArray});

    FUNCTION_TEST_RETURN_VOID();
}

void
pckReadArrayEnd(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (lstSize(this->tagStack) == 1 || this->tagStackTop->type != pckTypeArray)
        THROW(FormatError, "not in array");

    // Make sure we are at the end of the array
    unsigned int id = 0xFFFFFFFF - 1;
    pckReadTag(this, &id, pckTypeUnknown, true);

    // Pop array off the stack
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    // Reset tagNextId to keep reading
    this->tagNextId = 0;

    FUNCTION_TEST_RETURN_VOID();
}

Buffer *
pckReadBin(PackRead *this, PckReadBinParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(NULL);

    Buffer *result = NULL;

    if (pckReadTag(this, &param.id, pckTypeBin, false))
    {
        result = bufNew((size_t)pckReadUInt64Internal(this));

        while (bufUsed(result) < bufSize(result))
        {
            size_t size = pckReadBuffer(this, bufRemains(result));
            bufCatC(result, this->bufferPtr, this->bufferPos, size);
            this->bufferPos += size;
        }
    }
    else
        result = bufNew(0);

    FUNCTION_TEST_RETURN(result);
}

/**********************************************************************************************************************************/
bool
pckReadBool(PackRead *this, PckReadBoolParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(BOOL, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(pckReadTag(this, &param.id, pckTypeBool, false));
}

/**********************************************************************************************************************************/
int32_t
pckReadI32(PackRead *this, PckReadInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(INT, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(cvtInt32FromZigZag((uint32_t)pckReadTag(this, &param.id, pckTypeI32, false)));
}

/**********************************************************************************************************************************/
int64_t
pckReadI64(PackRead *this, PckReadInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(INT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(cvtInt64FromZigZag(pckReadTag(this, &param.id, pckTypeI64, false)));
}

/**********************************************************************************************************************************/
void
pckReadObjBegin(PackRead *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    pckReadTag(this, &param.id, pckTypeObj, false);
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});

    FUNCTION_TEST_RETURN_VOID();
}

void
pckReadObjEnd(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (lstSize(this->tagStack) == 1 || ((PackTagStack *)lstGetLast(this->tagStack))->type != pckTypeObj)
        THROW(FormatError, "not in object");

    // Make sure we are at the end of the object
    unsigned id = 0xFFFFFFFF - 1;
    pckReadTag(this, &id, pckTypeUnknown, true);

    // Pop object off the stack
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    // Reset tagNextId to keep reading
    this->tagNextId = 0;

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
void *
pckReadPtr(PackRead *this, PckReadPtrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(NULL);

    FUNCTION_TEST_RETURN((void *)(uintptr_t)pckReadTag(this, &param.id, pckTypePtr, false));
}

/**********************************************************************************************************************************/
String *
pckReadStr(PackRead *this, PckReadStrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(STRING, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(strDup(param.defaultValue));

    String *result = NULL;

    if (pckReadTag(this, &param.id, pckTypeStr, false))
    {
        size_t sizeExpected = (size_t)pckReadUInt64Internal(this);
        result = strNew("");

        while (strSize(result) != sizeExpected)
        {
            size_t sizeRead = pckReadBuffer(this, sizeExpected - strSize(result));
            strCatZN(result, (char *)this->bufferPtr + this->bufferPos, sizeRead);
            this->bufferPos += sizeRead;
        }
    }
    else
        result = strNew("");

    FUNCTION_TEST_RETURN(result);
}

/**********************************************************************************************************************************/
time_t
pckReadTime(PackRead *this, PckReadTimeParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(TIME, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN((time_t)cvtInt64FromZigZag(pckReadTag(this, &param.id, pckTypeTime, false)));
}

/**********************************************************************************************************************************/
uint32_t
pckReadU32(PackRead *this, PckReadUInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(UINT32, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN((uint32_t)pckReadTag(this, &param.id, pckTypeU32, false));
}

/**********************************************************************************************************************************/
uint64_t
pckReadU64(PackRead *this, PckReadUInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(UINT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (pckReadDefaultNull(this, param.defaultNull, &param.id))
        FUNCTION_TEST_RETURN(param.defaultValue);

    FUNCTION_TEST_RETURN(pckReadTag(this, &param.id, pckTypeU64, false));
}

/**********************************************************************************************************************************/
void
pckReadEnd(PackRead *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_READ, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    while (lstSize(this->tagStack) > 0)
    {
        // Make sure we are at the end of the container
        unsigned int id = 0xFFFFFFFF - 1;
        pckReadTag(this, &id, pckTypeUnknown, true);

        // Remove from stack
        lstRemoveLast(this->tagStack);
    }

    this->tagStackTop = NULL;

    FUNCTION_TEST_RETURN_VOID();
}

/**********************************************************************************************************************************/
String *
pckReadToLog(const PackRead *this)
{
    return strNewFmt(
        "{depth: %u, idLast: %u, tagNextId: %u, tagNextType: %u, tagNextValue %" PRIu64 "}", lstSize(this->tagStack),
        this->tagStackTop != NULL ? this->tagStackTop->idLast : 0, this->tagNextId, this->tagNextType, this->tagNextValue);
}

/**********************************************************************************************************************************/
static PackWrite *
pckWriteNewInternal(void)
{
    FUNCTION_TEST_VOID();

    PackWrite *this = NULL;

    MEM_CONTEXT_NEW_BEGIN("PackWrite")
    {
        this = memNew(sizeof(PackWrite));

        *this = (PackWrite)
        {
            .memContext = MEM_CONTEXT_NEW(),
            .tagStack = lstNewP(sizeof(PackTagStack)),
        };

        this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});
    }
    MEM_CONTEXT_NEW_END();

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteNew(IoWrite *write)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(IO_WRITE, write);
    FUNCTION_TEST_END();

    ASSERT(write != NULL);

    PackWrite *this = pckWriteNewInternal();
    this->write = write;
    this->buffer = bufNew(ioBufferSize());

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteNewBuf(Buffer *buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(buffer != NULL);

    PackWrite *this = pckWriteNewInternal();

    MEM_CONTEXT_BEGIN(this->memContext)
    {
        this->buffer = buffer;
    }
    MEM_CONTEXT_END();

    FUNCTION_TEST_RETURN(this);
}

/***********************************************************************************************************************************
!!! NEED TO IMPLEMENT WRITE DIRECTLY TO BUFFER AND WRITE TO IOWRITE()
***********************************************************************************************************************************/
static void pckWriteBuffer(PackWrite *this, const Buffer *buffer)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BUFFER, buffer);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (this->write == NULL)
    {
        if (bufRemains(this->buffer) < bufUsed(buffer))
            bufResize(this->buffer, (bufSizeAlloc(this->buffer) + bufUsed(buffer)) * 2);

        bufCat(this->buffer, buffer);
    }
    else
    {
        if (bufRemains(this->buffer) >= bufUsed(buffer))
            bufCat(this->buffer, buffer);
        else
        {
            if (bufUsed(this->buffer) > 0)
            {
                ioWrite(this->write, this->buffer);
                bufUsedZero(this->buffer);
            }

            if (bufRemains(this->buffer) >= bufUsed(buffer))
                bufCat(this->buffer, buffer);
            else
                ioWrite(this->write, buffer);
        }
    }

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
Pack an unsigned 64-bit integer to base-128 varint encoding
***********************************************************************************************************************************/
static void
pckWriteUInt64Internal(PackWrite *this, uint64_t value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT64, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    unsigned char buffer[PACK_UINT64_SIZE_MAX];
    size_t size = 0;

    while (value >= 0x80)
    {
        buffer[size] = (unsigned char)value | 0x80;
        value >>= 7;
        size++;
    }

    buffer[size] = (unsigned char)value;

    pckWriteBuffer(this, BUF(buffer, size + 1));

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
!!!
***********************************************************************************************************************************/
static void
pckWriteTag(PackWrite *this, PackType type, unsigned int id, uint64_t value)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(ENUM, type);
        FUNCTION_TEST_PARAM(UINT, id);
        FUNCTION_TEST_PARAM(UINT64, value);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (id == 0)
        id = this->tagStackTop->idLast + this->tagStackTop->nullTotal + 1;
    else
    {
        ASSERT(id > this->tagStackTop->idLast);
    }

    this->tagStackTop->nullTotal = 0;

    uint64_t tag = type << 4;
    unsigned int tagId = id - this->tagStackTop->idLast - 1;

    if (packTypeData[type].valueMultiBit)
    {
        if (value < 2)
        {
            tag |= (value & 0x1) << 2;
            value >>= 1;

            tag |= tagId & 0x1;
            tagId >>= 1;

            if (tagId > 0)
                tag |= 0x2;
        }
        else
        {
            tag |= 0x8;

            tag |= tagId & 0x3;
            tagId >>= 2;

            if (tagId > 0)
                tag |= 0x4;
        }
    }
    else if (packTypeData[type].valueSingleBit)
    {
        tag |= (value & 0x1) << 3;
        value >>= 1;

        tag |= tagId & 0x3;
        tagId >>= 2;

        if (tagId > 0)
            tag |= 0x4;
    }
    else
    {
        ASSERT(value == 0);

        tag |= tagId & 0x7;
        tagId >>= 3;

        if (tagId > 0)
            tag |= 0x8;
    }

    uint8_t tagByte = (uint8_t)tag;
    pckWriteBuffer(this, BUF(&tagByte, 1));

    if (tagId > 0)
        pckWriteUInt64Internal(this, tagId);

    if (value > 0)
        pckWriteUInt64Internal(this, value);

    this->tagStackTop->idLast = id;

    FUNCTION_TEST_RETURN_VOID();
}

/***********************************************************************************************************************************
!!!
***********************************************************************************************************************************/
static inline bool
pckWriteDefaultNull(PackWrite *this, bool defaultNull, bool defaultEqual)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BOOL, defaultNull);
        FUNCTION_TEST_PARAM(BOOL, defaultEqual);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (defaultNull && defaultEqual)
    {
        this->tagStackTop->nullTotal++;
        FUNCTION_TEST_RETURN(true);
    }

    FUNCTION_TEST_RETURN(false);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteNull(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    this->tagStackTop->nullTotal++;

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteArrayBegin(PackWrite *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    pckWriteTag(this, pckTypeArray, param.id, 0);
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeArray});

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteArrayEnd(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(lstSize(this->tagStack) != 1);
    ASSERT(((PackTagStack *)lstGetLast(this->tagStack))->type == pckTypeArray);

    pckWriteUInt64Internal(this, 0);
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteBin(PackWrite *this, const Buffer *value, PckWriteBinParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BUFFER, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
    FUNCTION_TEST_END();

    if (!pckWriteDefaultNull(this, param.defaultNull, value == NULL))
    {
        ASSERT(value != NULL);

        pckWriteTag(this, pckTypeBin, param.id, bufUsed(value) > 0);

        if (bufUsed(value) > 0)
        {
            pckWriteUInt64Internal(this, bufUsed(value));
            pckWriteBuffer(this, value);
        }
    }

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteBool(PackWrite *this, bool value, PckWriteBoolParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(BOOL, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(BOOL, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultNull, value == param.defaultValue))
        pckWriteTag(this, pckTypeBool, param.id, value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteI32(PackWrite *this, int32_t value, PckWriteInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(INT, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(INT, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultNull, value == param.defaultValue))
        pckWriteTag(this, pckTypeI32, param.id, cvtInt32ToZigZag(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteI64(PackWrite *this, int64_t value, PckWriteInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(INT64, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(INT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultNull, value == param.defaultValue))
        pckWriteTag(this, pckTypeI64, param.id, cvtInt64ToZigZag(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteObjBegin(PackWrite *this, PackIdParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT, param.id);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    pckWriteTag(this, pckTypeObj, param.id, 0);
    this->tagStackTop = lstAdd(this->tagStack, &(PackTagStack){.type = pckTypeObj});

    FUNCTION_TEST_RETURN(this);
}

PackWrite *
pckWriteObjEnd(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(lstSize(this->tagStack) != 1);
    ASSERT(((PackTagStack *)lstGetLast(this->tagStack))->type == pckTypeObj);

    pckWriteUInt64Internal(this, 0);
    lstRemoveLast(this->tagStack);
    this->tagStackTop = lstGetLast(this->tagStack);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWritePtr(PackWrite *this, const void *value, PckWritePtrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM_P(VOID, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultNull, value == NULL))
        pckWriteTag(this, pckTypePtr, param.id, (uintptr_t)value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteStr(PackWrite *this, const String *value, PckWriteStrParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(STRING, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(STRING, param.defaultValue);
    FUNCTION_TEST_END();

    if (!pckWriteDefaultNull(this, param.defaultNull, strEq(value, param.defaultValue)))
    {
        ASSERT(value != NULL);

        pckWriteTag(this, pckTypeStr, param.id, strSize(value) > 0);

        if (strSize(value) > 0)
        {
            pckWriteUInt64Internal(this, strSize(value));
            pckWriteBuffer(this, BUF(strZ(value), strSize(value)));
        }
    }

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteTime(PackWrite *this, time_t value, PckWriteTimeParam param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(TIME, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(TIME, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultNull, value == param.defaultValue))
        pckWriteTag(this, pckTypeTime, param.id, cvtInt64ToZigZag(value));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteU32(PackWrite *this, uint32_t value, PckWriteUInt32Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT32, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(UINT32, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultNull, value == param.defaultValue))
        pckWriteTag(this, pckTypeU32, param.id, value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteU64(PackWrite *this, uint64_t value, PckWriteUInt64Param param)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
        FUNCTION_TEST_PARAM(UINT64, value);
        FUNCTION_TEST_PARAM(UINT, param.id);
        FUNCTION_TEST_PARAM(BOOL, param.defaultNull);
        FUNCTION_TEST_PARAM(UINT64, param.defaultValue);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);

    if (!pckWriteDefaultNull(this, param.defaultNull, value == param.defaultValue))
        pckWriteTag(this, pckTypeU64, param.id, value);

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
PackWrite *
pckWriteEnd(PackWrite *this)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(PACK_WRITE, this);
    FUNCTION_TEST_END();

    ASSERT(this != NULL);
    ASSERT(lstSize(this->tagStack) == 1);

    pckWriteUInt64Internal(this, 0);
    this->tagStackTop = NULL;

    // If writing to io flush the internal buffer
    if (this->write != NULL)
    {
        if (bufUsed(this->buffer) > 0)
            ioWrite(this->write, this->buffer);
    }
    // Else resize the external buffer to trim off extra space added during processing
    else
        bufResize(this->buffer, bufUsed(this->buffer));

    FUNCTION_TEST_RETURN(this);
}

/**********************************************************************************************************************************/
String *
pckWriteToLog(const PackWrite *this)
{
    return strNewFmt(
        "{depth: %u, idLast: %u}", this->tagStackTop == NULL ? 0 : lstSize(this->tagStack),
        this->tagStackTop == NULL ? 0 : this->tagStackTop->idLast);
}

/**********************************************************************************************************************************/
const String *
pckTypeToStr(PackType type)
{
    FUNCTION_TEST_BEGIN();
        FUNCTION_TEST_PARAM(ENUM, type);
    FUNCTION_TEST_END();

    ASSERT(type < sizeof(packTypeData) / sizeof(PackTypeData));

    FUNCTION_TEST_RETURN(packTypeData[type].name);
}