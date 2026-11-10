#include "valheim.h"
#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef VALHEIM_ASSEMBLY_HASH
#define VALHEIM_ASSEMBLY_HASH "6dd6f2d79a9ae1489904bd9ec92fbdb67030cd095dd98b170817098fc181343b"
#define VALHEIM_ASSEMBLY_PATCHED "370202040c1db1f95d380cc3e698c72238f29c61b4f44b80f4d943e00a15bec3"
#define VALHEIM_PLAYFAB_HASH "88e39787d1b66b64c0a89514b4a6d635659f9198c41146c5ecab303213f9eae6"
#define VALHEIM_PLAYFAB_PATCHED "655c5d05f6fdb2461429277a6382d96e1dfcb6918d904be098c358ef54ceaa1c"
#endif

typedef struct { unsigned char *data; size_t size; mode_t mode; } ValheimBlob;
typedef struct {
    char path[PATH_MAX], backup[PATH_MAX], stage[PATH_MAX];
    ValheimBlob current, original, patched;
    bool has_backup, changed, active;
} ValheimFile;

static int valheim_read(const char *path, ValheimBlob *blob) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return errno;
    struct stat info;
    int result = fstat(fd, &info) < 0 ? errno : 0;
    if (!result && (!S_ISREG(info.st_mode) || info.st_size < 1 || info.st_size > 64 * 1024 * 1024)) result = ENOTSUP;
    if (!result) {
        blob->size = (size_t)info.st_size; blob->mode = info.st_mode;
        blob->data = malloc(blob->size);
        if (!blob->data) result = ENOMEM;
    }
    size_t used = 0;
    while (!result && used < blob->size) {
        ssize_t n = read(fd, blob->data + used, blob->size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { result = n < 0 ? errno : EIO; break; }
        used += (size_t)n;
    }
    close(fd);
    return result;
}

static bool valheim_equal(const ValheimBlob *a, const ValheimBlob *b) {
    return a->size == b->size && !memcmp(a->data, b->data, a->size);
}

static bool valheim_hash(const ValheimBlob *blob, const char *expected) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH]; char hex[65];
    CC_SHA256(blob->data, (CC_LONG)blob->size, digest);
    for (size_t i = 0; i < sizeof(digest); ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
    return !strcmp(hex, expected);
}

static int valheim_write(int fd, const ValheimBlob *blob) {
    size_t used = 0;
    while (used < blob->size) {
        ssize_t n = write(fd, blob->data + used, blob->size - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return n < 0 ? errno : EIO;
        used += (size_t)n;
    }
    return fsync(fd) == 0 ? 0 : errno;
}

static int valheim_stage(ValheimFile *file, const ValheimBlob *blob) {
    if (*file->stage) unlink(file->stage);
    if (snprintf(file->stage, sizeof(file->stage), "%s.onlinefix-valheim-XXXXXX", file->path) >= (int)sizeof(file->stage)) return ENAMETOOLONG;
    int fd = mkstemp(file->stage);
    int result = fd < 0 ? errno : 0;
    if (!result && fchmod(fd, file->current.mode & 0777) < 0) result = errno;
    if (!result) result = valheim_write(fd, blob);
    if (fd >= 0 && close(fd) < 0 && !result) result = errno;
    return result;
}

static int valheim_replace(ValheimFile *file, const ValheimBlob *expected) {
    ValheimBlob actual = {0};
    int result = valheim_read(file->path, &actual);
    if (!result && !valheim_equal(&actual, expected)) result = ESTALE;
    if (!result && rename(file->stage, file->path) < 0) result = errno;
    free(actual.data);
    return result;
}

static int valheim_bytes(ValheimBlob *blob, size_t offset, const unsigned char *bytes, size_t size) {
    if (offset > blob->size || size > blob->size - offset) return ENOTSUP;
    memcpy(blob->data + offset, bytes, size);
    return 0;
}

static int valheim_assembly(ValheimBlob *blob) {
    static const unsigned char load[] = {0x1a, 0x20, 0x2a, 0xa0, 0x0d, 0x00, 0x2a};
    static const unsigned char awake[] = {0x16};
    static const unsigned char login[] = {
        0x28, 0x42, 0x02, 0x00, 0x0a, 0x6f, 0x73, 0x02, 0x00, 0x0a, 0x6f, 0x74,
        0x02, 0x00, 0x0a, 0x8c, 0x61, 0x00, 0x00, 0x01, 0x6f, 0x48, 0x00, 0x00,
        0x0a, 0x80, 0x6b, 0x13, 0x00, 0x04, 0x28, 0x09, 0x13, 0x00, 0x06, 0x14,
        0xfe, 0x06, 0xe5, 0x13, 0x00, 0x06, 0x73, 0x45, 0x29, 0x00, 0x06, 0x6f,
        0x3d, 0x13, 0x00, 0x06, 0x2a
    };
    if (blob->size < 0x975ad) return ENOTSUP;
    int result = valheim_bytes(blob, 0x9747c, load, sizeof(load));
    if (!result) result = valheim_bytes(blob, 0x9759e, awake, sizeof(awake));
    if (!result) {
        memset(blob->data + 0x9759e + 1, 0x00, 14);
        result = valheim_bytes(blob, 0x92778, login, sizeof(login));
    }
    if (!result) memset(blob->data + 0x92778 + sizeof(login), 0x00, 0x4a - sizeof(login));
    return result;
}

static int valheim_playfab(ValheimBlob *blob) {
    static const unsigned char login[] = {
        0x73, 0xe8, 0x09, 0x00, 0x06, 0x25, 0x02, 0x7b, 0x56, 0x16, 0x00, 0x04,
        0x7d, 0xe8, 0x15, 0x00, 0x04, 0x25, 0x02, 0x7b, 0x54, 0x16, 0x00, 0x04,
        0x7d, 0xeb, 0x15, 0x00, 0x04, 0x03, 0x04, 0x05, 0x0e, 0x04, 0x28, 0xf8,
        0x01, 0x00, 0x06, 0x2a
    };
    if (blob->size < 0xd5a3) return ENOTSUP;
    int result = valheim_bytes(blob, 0xd53c, login, sizeof(login));
    if (!result) memset(blob->data + 0xd53c + sizeof(login), 0x00, 0x47 - sizeof(login));
    return result;
}

static int valheim_bundle(const char *app) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/Contents/Info.plist", app) >= (int)sizeof(path)) return ENAMETOOLONG;
    ValheimBlob info = {0};
    int result = valheim_read(path, &info);
    if (!result) {
        CFDataRef data = CFDataCreate(NULL, info.data, (CFIndex)info.size);
        CFPropertyListRef plist = data ? CFPropertyListCreateWithData(NULL, data, kCFPropertyListImmutable, NULL, NULL) : NULL;
        CFTypeRef identifier = plist && CFGetTypeID(plist) == CFDictionaryGetTypeID()
            ? CFDictionaryGetValue(plist, CFSTR("CFBundleIdentifier")) : NULL;
        if (!identifier || !CFEqual(identifier, CFSTR("com.coffeestain.valheim-steam"))) result = ENOTSUP;
        if (plist) CFRelease(plist);
        if (data) CFRelease(data);
    }
    free(info.data);
    return result;
}

static int valheim_apply(const char *app, int mode, bool *enabled) {
    if (!app) return EINVAL;
    char root[PATH_MAX], lock_path[PATH_MAX];
    if (!realpath(app, root)) return errno;
    int result = valheim_bundle(root);
    if (result) return result;
    if (snprintf(lock_path, sizeof(lock_path), "%s.onlinefix-valheim.lock", root) >= (int)sizeof(lock_path)) return ENAMETOOLONG;
    int lock = open(lock_path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (lock < 0) return errno;
    if (flock(lock, LOCK_EX | LOCK_NB) < 0) { result = errno; close(lock); return result; }

    ValheimFile files[2] = {0};
    const char *relative[] = {
        "Contents/Resources/Data/Managed/assembly_valheim.dll",
        "Contents/Resources/Data/Managed/PlayFab.dll"
    };
    const char *suffix[] = {".onlinefix-valheim-backup", ".onlinefix-valheim-backup"};
    const char *hash[] = {VALHEIM_ASSEMBLY_HASH, VALHEIM_PLAYFAB_HASH};
    const char *patched_hash[] = {VALHEIM_ASSEMBLY_PATCHED, VALHEIM_PLAYFAB_PATCHED};
    int (*patch[])(ValheimBlob *) = {valheim_assembly, valheim_playfab};

    for (size_t i = 0; i < 2 && !result; ++i) {
        ValheimFile *file = &files[i];
        if (snprintf(file->path, sizeof(file->path), "%s/%s", root, relative[i]) >= (int)sizeof(file->path) ||
            snprintf(file->backup, sizeof(file->backup), "%s%s", file->path, suffix[i]) >= (int)sizeof(file->backup)) { result = ENAMETOOLONG; break; }
        result = valheim_read(file->path, &file->current);
        if (result) break;
        result = valheim_read(file->backup, &file->original);
        file->has_backup = result == 0;
        if (result == ENOENT) { result = 0; file->original = file->current; }
        if (result) break;
        if (!valheim_hash(&file->original, hash[i])) { result = file->has_backup ? ESTALE : ENOTSUP; break; }
        file->patched = file->original;
        file->patched.data = malloc(file->original.size);
        if (!file->patched.data) { result = ENOMEM; break; }
        memcpy(file->patched.data, file->original.data, file->original.size);
        result = patch[i](&file->patched);
        if (result) break;
        if (!valheim_hash(&file->patched, patched_hash[i])) { result = ENOTSUP; break; }
        file->active = valheim_equal(&file->current, &file->patched);
        if (!file->active && !valheim_equal(&file->current, &file->original)) result = ESTALE;
    }

    if (!result && mode < 0) {
        if (files[0].active != files[1].active) result = ESTALE;
        else *enabled = files[0].active;
    }
    for (size_t i = 0; i < 2 && !result && mode >= 0; ++i) {
        ValheimFile *file = &files[i];
        const ValheimBlob *target = mode ? &file->patched : &file->original;
        result = valheim_stage(file, target);
        if (!result && mode && !file->has_backup) {
            int fd = open(file->backup, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
            if (fd < 0) result = errno;
            else {
                result = valheim_write(fd, &file->original);
                if (close(fd) < 0 && !result) result = errno;
                if (result) unlink(file->backup);
            }
        }
    }
    for (size_t i = 0; i < 2 && !result && mode >= 0; ++i) {
        ValheimFile *file = &files[i];
        if (valheim_equal(&file->current, mode ? &file->patched : &file->original)) continue;
        result = valheim_replace(file, &file->current);
        file->changed = result == 0;
    }
    if (!result && mode >= 0) {
        for (size_t i = 0; i < 2 && !result; ++i) {
            ValheimBlob actual = {0};
            result = valheim_read(files[i].path, &actual);
            if (!result && !valheim_equal(&actual, mode ? &files[i].patched : &files[i].original)) result = EIO;
            free(actual.data);
        }
    }
    if (result && mode >= 0) {
        for (size_t i = 2; i-- > 0;) {
            if (!files[i].changed) continue;
            int rollback = valheim_stage(&files[i], &files[i].current);
            if (!rollback) rollback = valheim_replace(&files[i], mode ? &files[i].patched : &files[i].original);
            if (rollback) result = rollback;
        }
    }
    if (!result && mode == 0) {
        for (size_t i = 0; i < 2; ++i) if (unlink(files[i].backup) < 0 && errno != ENOENT && !result) result = errno;
    }
    for (size_t i = 0; i < 2; ++i) {
        if (*files[i].stage) unlink(files[i].stage);
        if (files[i].original.data != files[i].current.data) free(files[i].original.data);
        free(files[i].current.data); free(files[i].patched.data);
    }
    close(lock);
    return result;
}

int of_valheim_patch(const char *app, bool enabled) { return valheim_apply(app, enabled, NULL); }
int of_valheim_status(const char *app, bool *enabled) {
    if (!enabled) return EINVAL;
    return valheim_apply(app, -1, enabled);
}
