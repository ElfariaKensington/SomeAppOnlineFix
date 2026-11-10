#include <assert.h>
#include <errno.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

#define VALHEIM_ASSEMBLY_HASH "3842667d2812b2c03e3f6b2af87bcb4133ac386ae05d79b4896e60926ff14770"
#define VALHEIM_ASSEMBLY_PATCHED "d6269d1483ca365ca77d704a3b88ceae2eca716406ac7a2f623ce3f13d51f069"
#define VALHEIM_PLAYFAB_HASH "017ba195cf4061d6e217c9056934a8edcd724893a57c156a1db607b3e9a69144"
#define VALHEIM_PLAYFAB_PATCHED "affc5187fddb299c26a7dee28d3774c2e679f6f3867fe015578d9896b97d5af7"
#include "../src/patch/gamespecific/valheim.c"

static void write_fixture(const char *path, const ValheimBlob *blob) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    assert(fd >= 0 && valheim_write(fd, blob) == 0 && close(fd) == 0);
}
static void check_file(const char *path, const ValheimBlob *expected) {
    ValheimBlob actual = {0};
    assert(valheim_read(path, &actual) == 0 && valheim_equal(&actual, expected));
    free(actual.data);
}
static void make_directory(const char *path) {
    char parent[PATH_MAX];
    assert(strlen(path) < sizeof(parent)); strcpy(parent, path);
    for (char *p = parent + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0'; assert(mkdir(parent, 0755) == 0 || errno == EEXIST); *p = '/';
    }
    assert(mkdir(parent, 0755) == 0 || errno == EEXIST);
}
static int remove_fixture(const char *path, const struct stat *info, int flag, struct FTW *walk) { return remove(path); }

int main(void) {
    char root[] = "/private/tmp/onlinefix-valheim-XXXXXX";
    assert(mkdtemp(root));
    char app[PATH_MAX], managed[PATH_MAX], assembly[PATH_MAX], playfab[PATH_MAX], info[PATH_MAX];
    char assembly_backup[PATH_MAX], playfab_backup[PATH_MAX], lock_path[PATH_MAX];
    snprintf(app, sizeof(app), "%s/valheim.app", root);
    snprintf(managed, sizeof(managed), "%s/Contents/Resources/Data/Managed", app); make_directory(managed);
    snprintf(assembly, sizeof(assembly), "%s/assembly_valheim.dll", managed);
    snprintf(playfab, sizeof(playfab), "%s/PlayFab.dll", managed);
    snprintf(info, sizeof(info), "%s/Contents/Info.plist", app);
    snprintf(assembly_backup, sizeof(assembly_backup), "%s.onlinefix-valheim-backup", assembly);
    snprintf(playfab_backup, sizeof(playfab_backup), "%s.onlinefix-valheim-backup", playfab);
    snprintf(lock_path, sizeof(lock_path), "%s.onlinefix-valheim.lock", app);
    unsigned char plist[] = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>CFBundleIdentifier</key><string>com.coffeestain.valheim-steam</string></dict></plist>";
    ValheimBlob bundle_info = {.data = plist, .size = sizeof(plist) - 1}; write_fixture(info, &bundle_info);

    ValheimBlob original_assembly = {.size = 0x97600}, original_playfab = {.size = 0xd600};
    original_assembly.data = calloc(1, original_assembly.size);
    original_playfab.data = calloc(1, original_playfab.size);
    assert(original_assembly.data && original_playfab.data);
    memcpy(original_assembly.data + 0x9747c, "\x1b\x30\x02\x00\x53\x00\x00\x00", 8);
    memcpy(original_assembly.data + 0x9759e, "\x7e\x25\x14\x00\x04\x28", 6);
    memcpy(original_assembly.data + 0x92778, "\x73\x00\x00\x00\x00\x0c", 6);
    memcpy(original_playfab.data + 0xd530, "\x13\x30\x0a\x00\x47\x00\x00\x00", 8);
    assert(valheim_hash(&original_assembly, VALHEIM_ASSEMBLY_HASH));
    assert(valheim_hash(&original_playfab, VALHEIM_PLAYFAB_HASH));
    write_fixture(assembly, &original_assembly); write_fixture(playfab, &original_playfab);

    bool enabled = true;
    assert(of_valheim_status(app, &enabled) == 0 && !enabled);
    assert(access(assembly_backup, F_OK) < 0 && access(playfab_backup, F_OK) < 0);
    assert(of_valheim_patch(app, true) == 0);
    assert(of_valheim_status(app, &enabled) == 0 && enabled);
    check_file(assembly_backup, &original_assembly); check_file(playfab_backup, &original_playfab);
    ValheimBlob patched_assembly = {0}, patched_playfab = {0};
    assert(valheim_read(assembly, &patched_assembly) == 0 && valheim_read(playfab, &patched_playfab) == 0);
    assert(valheim_hash(&patched_assembly, VALHEIM_ASSEMBLY_PATCHED));
    assert(valheim_hash(&patched_playfab, VALHEIM_PLAYFAB_PATCHED));
    assert(of_valheim_patch(app, true) == 0);
    check_file(assembly, &patched_assembly); check_file(playfab, &patched_playfab);
    assert(of_valheim_patch(app, false) == 0);
    check_file(assembly, &original_assembly); check_file(playfab, &original_playfab);
    assert(access(assembly_backup, F_OK) < 0 && access(playfab_backup, F_OK) < 0);
    assert(of_valheim_patch(app, false) == 0);

    assert(of_valheim_patch(app, true) == 0);
    write_fixture(assembly, &original_assembly);
    assert(of_valheim_status(app, &enabled) == ESTALE);
    assert(of_valheim_patch(app, true) == 0);
    patched_assembly.data[0] ^= 1; write_fixture(assembly, &patched_assembly);
    assert(of_valheim_patch(app, false) == ESTALE && of_valheim_patch(app, true) == ESTALE);
    check_file(assembly, &patched_assembly); check_file(playfab, &patched_playfab);
    check_file(assembly_backup, &original_assembly);
    patched_assembly.data[0] ^= 1; write_fixture(assembly, &patched_assembly);
    assert(of_valheim_patch(app, false) == 0);

    assert(symlink(assembly, assembly_backup) == 0);
    assert(of_valheim_patch(app, true) == ELOOP);
    assert(access(playfab_backup, F_OK) < 0); assert(unlink(assembly_backup) == 0);
    int lock = open(lock_path, O_RDWR | O_CREAT, 0600); assert(lock >= 0 && flock(lock, LOCK_EX | LOCK_NB) == 0);
    assert(of_valheim_patch(app, true) == EWOULDBLOCK); close(lock);
    assert(of_valheim_patch(NULL, true) == EINVAL && of_valheim_status(app, NULL) == EINVAL);
    *strstr((char *)plist, "com.coffeestain.valheim-steam") = 'x';
    write_fixture(info, &bundle_info); assert(of_valheim_patch(app, true) == ENOTSUP);

    free(original_assembly.data); free(original_playfab.data);
    free(patched_assembly.data); free(patched_playfab.data);
    assert(nftw(root, remove_fixture, 16, FTW_DEPTH | FTW_PHYS) == 0);
    return 0;
}
