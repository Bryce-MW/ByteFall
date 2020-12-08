//
// Created by Bryce Wilson on 11/24/20.
//

#include "constants.cpp"
#include <sodium.h>
#include "waterfall.cpp"
#include <cstring>
#include <sys/stat.h>
#include <sys/mman.h>
#include <libgen.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_PATCH 1

/**
 * TODO(bryce): General TODOS:
 *  * Handle errors properly
 *  * Pull waterfall_file creation out into a function
 */

struct file_type {
    bool32 isDirectory;
    bool32 isRegularFile;
};


// TODO(bryce):
//  * Get rid of struct stat and get the properties directly from the path in a platform
//    agnostic way
//  * Find a good way to get creation date if available and set to 0 otherwise
//  * Decide on a better form of permissions. Perhaps only supporting the basics of
//    whether the execute bit is on or not. Then synthesise it on Windows. There is just
//    so much space unused that I wasn't sure what to do with it all.
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
HashFile(waterfall_file* File, file_type FileType, c8* FilePath,
         c8* Names, uint32 NameSize) {
    crypto_generichash_state HashState;
    crypto_generichash_init(&HashState, nullptr, 0, HASH_SIZE);
    crypto_generichash_update(&HashState, (uint8*)(Names + File->NameIndex), NameSize);

    if (FileType.isRegularFile) {
        int32 FileNo = open(FilePath, O_RDONLY);
        if (FileNo == -1) {
            puts("File read failed!");
            puts(FilePath);
            return 0;
        }
        auto* FileMemory = (uint8*)mmap(nullptr, File->Size, PROT_READ, MAP_SHARED,
                                        FileNo, 0);
        close(FileNo);
        if (!FileMemory) {
            puts("File mmap failed!");
            return 0;
        }
        crypto_generichash_update(&HashState, FileMemory, File->Size);
        munmap(FileMemory, File->Size);
    }

    crypto_generichash_final(&HashState, File->Hash, HASH_SIZE);

    return 1;
}

// STUDY(bryce): Use something better for recursive name storage. Perhaps one malloc
//  before first call which is reallocated as needed.
internal uint32
ProcessTree(waterfall_file* Parent, FILE* WaterfallFile, c8* FilePath,
            waterfall_names_header* NamesHeader, c8** Names) {
    struct stat Properties;
    if (stat(FilePath, &Properties)) {
        puts("File not found!");
        puts(FilePath);
        return 0;
    }
    file_type FileType = GetFileType(&Properties);

    waterfall_file File = {};
    FillFileProperties(&File, &Properties, FileType);

    c8* FileName = basename(FilePath);
    uint32 FilePathLength = strlen(FileName) + 1;
    if (NamesHeader->Rows * 64 < NamesHeader->Size + FilePathLength) {
        NamesHeader->Rows++;
        *Names = (c8*)realloc(*Names, NamesHeader->Rows * 64);
    }
    File.NameIndex = NamesHeader->Size;
    memcpy(*Names + NamesHeader->Size, FileName,
           FilePathLength);
    NamesHeader->Size += FilePathLength;


    if (!HashFile(&File, FileType, FilePath, *Names, FilePathLength)) {
        return 0;
    }
    if (Parent) {
        memcpy(&File.ParentHash, &Parent->Hash, HASH_SIZE);
    }

    fwrite(&File, sizeof(waterfall_file), 1, WaterfallFile);
    uint32 Result = 1;

    if (FileType.isDirectory) {
        DIR* Dir = opendir(FilePath);
        struct dirent* Entry;
        if (!Dir) {
            return Result;
        }
        uint32 PathLength = strlen(FilePath);
        auto* NewPath = (c8*)malloc(PathLength + 1);
        // NOTE(bryce): This is not supposed to include the null terminator!
        memcpy(NewPath, FilePath, PathLength); // NOLINT(bugprone-not-null-terminated-result)
        while ((Entry = readdir(Dir))) {
            // TODO(bryce): Make these checks better
            if (!strcmp(Entry->d_name, ".") || !strcmp(Entry->d_name, "..") ||
                !strcmp(Entry->d_name, "/")) {
                continue;
            }
            uint32 NewPathLength = strlen(Entry->d_name);
            NewPath = (c8*)realloc(NewPath, PathLength + NewPathLength + 2);
            NewPath[PathLength] = '/';
            memcpy(NewPath + PathLength + 1, Entry->d_name, NewPathLength + 1);
            Result += ProcessTree(&File, WaterfallFile, NewPath, NamesHeader, Names);
        }
        free(NewPath);
        closedir(Dir);
    }

    return Result;
}

int
main(int32 argc, c8** argv) {
    if (argc != 3) {
        printf("Usage:\n%s dest.waterfall src", argv[0]);
    }
    c8* OutputFileName = argv[1];
    c8* SourceFileName = argv[2];
    if (sodium_init() < 0) {
        // NOTE(bryce): Crypto is required
        return -1;
    }

    waterfall_header Header = {};
    memcpy(Header.MagicString, MAGIC_STRING, 32);
    Header.VersionMajor = VERSION_MAJOR;
    Header.VersionMinor = VERSION_MINOR;
    Header.VersionPatch = VERSION_PATCH;
    strcpy(Header.Name, argv[2]);
    randombytes_buf(&Header.Salt, SALT_SIZE);
    crypto_generichash(Header.WaterfallHash, HASH_SIZE,
                       (uint8*)Header.Name, NAME_SIZE + SALT_SIZE,
                       nullptr, 0);
    crypto_sign_keypair(Header.PK, Header.SK);

    FILE* DebugOutput = fopen(argv[1], "wb");
    fwrite(&Header, sizeof(waterfall_header), 1, DebugOutput);

    waterfall_names_header NamesHeader = {};
    NamesHeader.Size = 0;
    NamesHeader.Rows = 1;
    auto* Names = (c8*)malloc(64);

    uint32 FileCount = ProcessTree(nullptr, DebugOutput, Header.Name,
                                   &NamesHeader, &Names);

    fwrite(&NamesHeader, sizeof(waterfall_names_header), 1, DebugOutput);
    fwrite(Names, NamesHeader.Size, 1, DebugOutput);
    free(Names);

    waterfall_footer Footer = {};
    fwrite(&Footer, sizeof(waterfall_footer), 1, DebugOutput);

    waterfall_peer BestPeers[BEST_PEER_COUNT] = {};
    fwrite(BestPeers, sizeof(waterfall_peer), BEST_PEER_COUNT, DebugOutput);

    // TODO(bryce): Add peers
    fclose(DebugOutput);


    int32 DebugOutputNo = open(argv[1], O_RDWR);
    waterfall Mapped = {};
    // TODO(bryce): This will need to account for real peer size too
    uint64 WaterfallFileSize = sizeof(waterfall_header) +
                               sizeof(waterfall_file) * FileCount +
                               sizeof(waterfall_names_header) +
                               NamesHeader.Rows * 64 +
                               sizeof(waterfall_footer) +
                               sizeof(waterfall_peer) * BEST_PEER_COUNT;
    Mapped.Header = (waterfall_header*)mmap(nullptr, WaterfallFileSize,
                                            PROT_READ | PROT_WRITE, MAP_SHARED,
                                            DebugOutputNo, 0);
    if (Mapped.Header == MAP_FAILED) {
        puts("Something went very wrong with the final mmap!");
    }
    close(DebugOutputNo);

    Mapped.Files = (waterfall_file*)(Mapped.Header + 1);
    Mapped.NamesHeader = (waterfall_names_header*)(Mapped.Files + FileCount);
    Mapped.Names = (uint8*)(Mapped.NamesHeader + 1);
    Mapped.Footer = (waterfall_footer*)(Mapped.Names + Mapped.NamesHeader->Rows * 64);
    Mapped.BestPeers = (waterfall_peer*)(Mapped.Footer + 1);
    Mapped.Peers = Mapped.BestPeers + BEST_PEER_COUNT;

    Mapped.Header->FileCount = FileCount;
    crypto_sign_detached(Mapped.Header->Signature, nullptr,
                         (uint8*)Mapped.Header,
                         (uint8*)&Mapped.Header->FileCount - (uint8*)Mapped.Header,
                         Mapped.Header->SK);

    crypto_sign_detached(Mapped.Footer->Signature, nullptr,
                         (uint8*)Mapped.Files,
                         sizeof(waterfall_file) * FileCount +
                         sizeof(waterfall_names_header) +
                         Mapped.NamesHeader->Rows * 64,
                         Mapped.Header->SK);

    munmap(Mapped.Header, WaterfallFileSize);

    return 0;
}
