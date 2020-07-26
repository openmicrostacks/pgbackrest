/* Generated by the protocol buffer compiler.  DO NOT EDIT! */
/* Generated from: manifestFileData.proto */
#include "build.auto.h"

#ifndef PROTOBUF_C_manifestFileData_2eproto__INCLUDED
#define PROTOBUF_C_manifestFileData_2eproto__INCLUDED

#include "info/protobuf.vendor.h"

PROTOBUF_C__BEGIN_DECLS

#if PROTOBUF_C_VERSION_NUMBER < 1003000
# error This file was generated by a newer version of protoc-c which is incompatible with your libprotobuf-c headers. Please update your headers.
#elif 1003003 < PROTOBUF_C_MIN_COMPILER_VERSION
# error This file was generated by an older version of protoc-c which is incompatible with your libprotobuf-c headers. Please regenerate this file with a newer version of protoc-c.
#endif


typedef struct _ManifestFileData ManifestFileData;


/* --- enums --- */


/* --- messages --- */

struct  _ManifestFileData
{
  ProtobufCMessage base;
  char *user_name;
  int64_t favourite_number;
  size_t n_interests;
  char **interests;
};
#define MANIFEST_FILE_DATA__INIT \
 { PROTOBUF_C_MESSAGE_INIT (&manifest_file_data__descriptor) \
    , (char *)protobuf_c_empty_string, 0, 0,NULL }


/* ManifestFileData methods */
void   manifest_file_data__init
                     (ManifestFileData         *message);
size_t manifest_file_data__get_packed_size
                     (const ManifestFileData   *message);
size_t manifest_file_data__pack
                     (const ManifestFileData   *message,
                      uint8_t             *out);
size_t manifest_file_data__pack_to_buffer
                     (const ManifestFileData   *message,
                      ProtobufCBuffer     *buffer);
ManifestFileData *
       manifest_file_data__unpack
                     (ProtobufCAllocator  *allocator,
                      size_t               len,
                      const uint8_t       *data);
void   manifest_file_data__free_unpacked
                     (ManifestFileData *message,
                      ProtobufCAllocator *allocator);
/* --- per-message closures --- */

typedef void (*ManifestFileData_Closure)
                 (const ManifestFileData *message,
                  void *closure_data);

/* --- services --- */


/* --- descriptors --- */

extern const ProtobufCMessageDescriptor manifest_file_data__descriptor;

PROTOBUF_C__END_DECLS


#endif  /* PROTOBUF_C_manifestFileData_2eproto__INCLUDED */