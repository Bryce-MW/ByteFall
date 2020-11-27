//
// Created by Bryce Wilson on 11/24/20.
//

#include "constants.cpp"
#include <sodium.h>
#include "waterfall.cpp"
#include <cstring>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ftw.h>

#define VERSION_MAJOR 0
#define VERSION_MINOR 0
#define VERSION_PATCH 0

/**
 * TODO(bryce): General TODOS:
 *  * Handle errors properly
 *  * Allow folders to have the same name
 *  * Make searching more efficient (possibly with local_persist last found?)
 *  * Pull waterfall_file creation out into a function
 *  * Change the format of files to reduce space taken by the name
 *    * I am thinking about moving the info and hash to the top and adding a field for
 *      number of rows (up to 4) of name. Though this makes reading the file harder
 *      perhaps I could store the names in a separate table and use an index into that
 *      table. That may be a lot easier to read by allowing me to read larger sections at
 *      once.
 */

struct file_type {
    bool32 isDirectory;
    bool32 isRegularFile;
};

global_variable waterfall_file_arena GlobalFiles;

// TODO(bryce):
//  * Get rid of struct stat and get the properties directly from the path in a platform
//    agnostic way
//  * Find a good way to get creation date if available and set to 0 otherwise
//  * Decide on a better form of permissions. Perhaps only supporting the basics of whether
//    the execute bit is on or not. Then synthesise it on Windows. There is just so much
//    space unused that I wasn't sure what to do with it all.
internal bool32
FillFileProperties(waterfall_file* File, struct stat* Properties, file_type FileType) {
    if (!(FileType.isDirectory || FileType.isRegularFile)) {
        return 0;
    }

    File->Size = FileType.isRegularFile ? Properties->st_size : 0;
    File->ModificationDate = Properties->st_mtime;
    File->PieceSize = File->Size;
    File->Version = 0;
    File->Permissions = Properties->st_mode;

    return 1;
}

internal file_type
GetFileType(struct stat* Properties) {
    file_type Result = {
            S_ISDIR(Properties->st_mode),
            S_ISREG(Properties->st_mode)
    };
    return Result;
}

internal int32
HashFile(waterfall_file* File, file_type FileType) {
    crypto_generichash_state HashState;
    crypto_generichash_init(&HashState, nullptr, 0, HASH_SIZE);
    crypto_generichash_update(&HashState, File->Name, NAME_SIZE);

    if (FileType.isRegularFile) {
        FILE* FileToHash = fopen((c8*)File->Name, "rb");
        if (!FileToHash) {
            puts("File read failed!");
            puts((c8*)File->Name);
            return 0;
        }
        auto* FileMemory = (uint8*)mmap(nullptr, File->Size, PROT_READ, MAP_SHARED,
                                        fileno(FileToHash), 0);
        fclose(FileToHash);
        if (!FileMemory) {
            puts("File mmap failed!");
            return 0;
        }
        crypto_generichash_update(&HashState, (uc8*)FileMemory, File->Size);
        munmap(FileMemory, File->Size);
    }

    crypto_generichash_final(&HashState, (uc8*)&File->Hash, HASH_SIZE);

    return 1;
}

int
AddFile(const char* Name, const struct stat* Properties,
        int type, struct FTW* Ftw) {
    local_persist waterfall_file* Parent = nullptr;

    file_type FileType = GetFileType((struct stat*)Properties);
    if (!(FileType.isRegularFile || FileType.isDirectory)) {
        return 0;
    }
    waterfall_file File = {};
    FillFileProperties(&File, (struct stat*)Properties, FileType);
    memcpy(File.Name, Name + Ftw->base, strlen(Name) - Ftw->base);
    if (!HashFile(&File, FileType)) {
        return 0;
    }
    if (Parent) {
        memcpy(&File.ParentHash, &Parent->Hash, HASH_SIZE);
    } else {
        puts("No parent found:");
        puts((c8*)File.Name);
    }

    if (GlobalFiles.Count == GlobalFiles.Capacity) {
        auto* NewFiles = (waterfall_file*)malloc(GlobalFiles.Capacity * 2 *
                                                 sizeof(waterfall_file));
        memcpy(NewFiles, GlobalFiles.Files,
               GlobalFiles.Count * sizeof(waterfall_file));
        free(GlobalFiles.Files);
        if (Parent) {
            Parent = NewFiles + (Parent - GlobalFiles.Files);
        }
        GlobalFiles.Files = NewFiles;
        GlobalFiles.Capacity *= 2;
    }
    GlobalFiles.Files[GlobalFiles.Count++] = File;

    if (FileType.isDirectory) {
        Parent = &GlobalFiles.Files[GlobalFiles.Count];
    }

    return 0;
}

internal bool32
Input(uint8* Prompt, uint8* Dest, uint64 max_size) {
    printf("%s", (char*)(Prompt));
    if (fgets((char*)Dest, max_size, stdin)) {
        for (uint64 StrIndex = max_size - 1; StrIndex >= 0; StrIndex--) {
            if (Dest[StrIndex]) {
                Dest[StrIndex] = 0;
                break;
            }
        }
        return true;
    } else {
        return false;
    }
}

int
main() {
    if (sodium_init() < 0) {
        return -1;
    }

    // TODO(bryce): Build this from arguments
    waterfall Waterfall = {};
    memcpy(Waterfall.Header.MagicString, MAGIC_STRING, 32);
    Waterfall.Header.VersionMajor = VERSION_MAJOR;
    Waterfall.Header.VersionMinor = VERSION_MINOR;
    Waterfall.Header.VersionPatch = VERSION_PATCH;
    Input((uint8*)"Waterfall Name ->", Waterfall.Header.Name, 64 * 4);
    randombytes_buf(&Waterfall.Header.Salt, SALT_SIZE);
    crypto_generichash(Waterfall.Header.WaterfallHash, HASH_SIZE,
                       Waterfall.Header.Name, NAME_SIZE + SALT_SIZE,
                       nullptr, 0);
    crypto_sign_keypair((uc8*)&Waterfall.Header.PK, (uc8*)&Waterfall.Header.SK);

    struct stat Properties;
    if (stat((c8*)Waterfall.Header.Name, &Properties)) {
        puts("File not found!");
        return -1;
    }
    bool32 isDirectory = S_ISDIR(Properties.st_mode);
    bool32 isRegularFile = S_ISREG(Properties.st_mode);
    waterfall_file SingleFile = {};
    if (isRegularFile) {
        Waterfall.Header.FileCount = 1;
        memcpy(SingleFile.Name, Waterfall.Header.Name, NAME_SIZE);
        SingleFile.Size = Properties.st_size;
        //SingleFile.CreationDate = Properties.st_birthtime;
        SingleFile.ModificationDate = Properties.st_mtime;
        // TODO(bryce): Figure out how we are going to do pieces
        SingleFile.PieceSize = SingleFile.Size;
        // NOTE(bryce): We don't do any updates yet so this will always be 0 for now
        SingleFile.Version = 0;
        // TODO(bryce): This is not really what I want here
        SingleFile.Permissions = Properties.st_mode;

        FILE* FileToHash = fopen((c8*)SingleFile.Name, "rb");
        auto* FileMemory = (uint8*)mmap(nullptr, SingleFile.Size, PROT_READ, MAP_SHARED,
                                        fileno(FileToHash), 0);

        if (!FileMemory) {
            puts("File read failed!");
            return -1;
        }

        crypto_generichash_state HashState;
        crypto_generichash_init(&HashState, nullptr, 0, HASH_SIZE);
        crypto_generichash_update(&HashState, SingleFile.Name, NAME_SIZE);
        crypto_generichash_update(&HashState, (uc8*)FileMemory, SingleFile.Size);
        crypto_generichash_final(&HashState, (uc8*)&SingleFile.Hash, HASH_SIZE);

        Waterfall.Files = &SingleFile;
    }
    if (isDirectory) {
        if (GlobalFiles.Files) {
            free(GlobalFiles.Files);
        }
        GlobalFiles.Files = (waterfall_file*)malloc(100 * sizeof(waterfall_file));
        GlobalFiles.Capacity = 100;
        GlobalFiles.Count = 0;
        nftw((c8*)Waterfall.Header.Name, AddFile, 10, FTW_MOUNT | FTW_CHDIR);
        Waterfall.Files = GlobalFiles.Files;
        Waterfall.Header.FileCount = GlobalFiles.Count;
    }


    crypto_sign_detached(Waterfall.Header.Signature, nullptr,
                         (uc8*)&Waterfall.Header,
                         sizeof(waterfall_header) - SIG_SIZE,
                         Waterfall.Header.SK);

    crypto_sign_state SignState;
    crypto_sign_init(&SignState);
    crypto_sign_update(&SignState, (uc8*)Waterfall.Files,
                       Waterfall.Header.FileCount * sizeof(waterfall_file));
    crypto_sign_final_create(&SignState, Waterfall.Footer.Signature, nullptr,
                             Waterfall.Header.SK);
    // TODO(bryce): Add peers


    FILE* DebugOutput = fopen("test.waterfall", "wb");
    fwrite(&Waterfall.Header, sizeof(waterfall_header), 1, DebugOutput);
    fwrite(Waterfall.Files, sizeof(waterfall_file), Waterfall.Header.FileCount,
           DebugOutput);
    fwrite(&Waterfall.Footer, sizeof(waterfall_footer), 1, DebugOutput);
    fwrite(Waterfall.BestPeers, sizeof(waterfall_peer), BEST_PEER_COUNT, DebugOutput);
    fwrite(Waterfall.Peers, sizeof(waterfall_peer), Waterfall.Footer.PeerCount,
           DebugOutput);


    return 0;
}
