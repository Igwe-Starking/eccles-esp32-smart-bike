/*
  ECCLES FILE SYSTEM: controls file access on LittleFS flash storage.
  Opens files, reads them in chunks, supports read/write/append modes.
  Backed by the esp_littlefs VFS component with POSIX FILE* (fopen/fread/fwrite/fseek/ftell).
  Every public method keeps its original name, signature and behaviour.
*/

#ifndef ECCLES_FILE_SYSTEM
#define ECCLES_FILE_SYSTEM

#include <cstdio>
#include "esp_littlefs.h"
#include "EcclesTypes.h"

ECCLES_API {

  enum class FileStatus {
    SUCCESSFUL,
    ERROR,
    END_OF_FILE,
    BAD_ARG,
    MOUNT_FAILED,
    FILE_DOESNT_EXIST,
    BAD_MODE
  };

  #define FILE_MAX_CHUNK_BUFFER 2048

  class FileSystem {
    private:
    e_string mode;
    static e_boolean mounted;
    FileStatus status = FileStatus::SUCCESSFUL;
    static e_uint8 u_count;
    FILE* file = nullptr;
    e_uint32 f_ptr;

    public:
    FileSystem();
    ~FileSystem();

    FileStatus load(e_string path, e_string mode);
    FileStatus read(e_uint8* buffer, e_uint16 len) __attribute__((nonnull));
    FileStatus chunk(e_uint8* buffer, e_uint32 offset, e_uint16 size) __attribute__((nonnull));
    FileStatus readAll(e_uint8* buffer, e_uint32 len) __attribute__((nonnull));
    e_uint32 getSize() __attribute__((always_inline));
    FileStatus write(e_uint8* buffer, e_uint32 len) __attribute__((nonnull));
    FileStatus append(e_uint8* buffer, e_uint32 len) __attribute__((nonnull));
    FileStatus insert(e_uint8* buffer, e_uint32 offset, e_uint32 len) __attribute__((nonnull));
    FILE* getFile();
    e_boolean exists() const;
    FileStatus skip(e_uint32 len);
    FileStatus unload();
    static e_string getStatusText(FileStatus s);
    explicit operator bool() const;
  };
};
#endif
