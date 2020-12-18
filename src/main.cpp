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
#include <copyfile.h>

#define VERSION_MAJOR 0
#define VERSION_MINOR 2
#define VERSION_PATCH 0

/*
 * TODO(bryce): General TODOS:
 *  * Handle errors properly
 *  * Pull waterfall_file creation out into a function
 *  * Add CF communication
 *  * Learn more about proper memory management to ensure that I am doing everything the
 *    the correct way to get the best performance
 *  * Possibly add a version tag to the header to quickly see if this is really a new
 *    version. I am not sure that I will need it though so I am holding off until I start
 *    working on the protocol.
 */

struct file_type {
    bool32 isDirectory;
    bool32 isRegularFile;
};


/*
 * TODO(bryce):
 *  * Get rid of struct stat and get the properties directly from the path in a platform
 *    agnostic way
 *  * Find a good way to get creation date if available and set to 0 otherwise
 *  * Decide on a better form of permissions. Perhaps only supporting the basics of
 *    whether the execute bit is on or not. Then synthesise it on Windows. There is just
 *  so much space unused that I wasn't sure what to do with it all.
 */
internal bool32
FillFileProperties(waterfall_file* File, struct stat* Properties, file_type FileType) {
    if (!(FileType.isDirectory || FileType.isRegularFile)) {
        return 0;
    }

    File->Size = FileType.isRegularFile ? Properties->st_size : 0;
    File->PieceNumber = 0;
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
ProcessTree(uint32 ParentIndex, uint32 OtherChildrenCount, FILE* WaterfallFile,
            c8* FilePath, waterfall_names_header* NamesHeader, c8** Names) {
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
    File.ParentIndex = ParentIndex;

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
            Result += ProcessTree(ParentIndex + OtherChildrenCount,
                                  Result - 1, WaterfallFile,
                                  NewPath, NamesHeader, Names);
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
    strcpy(Header.Name, SourceFileName);

    FILE* DebugOutput;
    bool32 UseTmpFile;
    c8* TmpFileName;
    if ((UseTmpFile = !access(OutputFileName, F_OK))) {
        c8 TmpFileNameTemplate[] = "tmp.waterfall.XXXXXX";
        TmpFileName = mktemp(TmpFileNameTemplate);
        // TODO(bryce): Put this in a proper tmp directory.
        if (copyfile(OutputFileName, TmpFileName, nullptr, COPYFILE_CLONE)) {
            puts("Could not create temporary file");
        }
    }
    DebugOutput = fopen(OutputFileName, "w+b");
    fwrite(&Header, sizeof(waterfall_header), 1, DebugOutput);

    if (!UseTmpFile) {
        randombytes_buf(&Header.Salt, SALT_SIZE);
        crypto_generichash(Header.WaterfallHash, HASH_SIZE,
                           (uint8*)Header.Name, NAME_SIZE + SALT_SIZE,
                           nullptr, 0);
        crypto_sign_keypair(Header.PK, Header.SK);
    }

    waterfall_names_header NamesHeader = {};
    NamesHeader.Size = 0;
    NamesHeader.Rows = 1;
    auto* Names = (c8*)malloc(64);

    uint32 FileCount = ProcessTree(0, 0,
                                   DebugOutput, Header.Name,
                                   &NamesHeader, &Names);

    fwrite(&NamesHeader, sizeof(waterfall_names_header), 1, DebugOutput);
    fwrite(Names, NamesHeader.Rows * 64, 1, DebugOutput);
    free(Names);

    waterfall_footer Footer = {};
    fwrite(&Footer, sizeof(waterfall_footer), 1, DebugOutput);

    waterfall_peer BestPeers[BEST_PEER_COUNT] = {};
    fwrite(BestPeers, sizeof(waterfall_peer), BEST_PEER_COUNT, DebugOutput);

    // TODO(bryce): Add peers

    fflush(DebugOutput);
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
                                            fileno(DebugOutput), 0);
    if (Mapped.Header == MAP_FAILED) {
        puts("Something went very wrong with the final mmap!");
        return -1;
    }

    // TODO(bryce): This is not well written and can easily break.
    if (!UseTmpFile) {
        fclose(DebugOutput);
    }

    Mapped.Files = (waterfall_file*)(Mapped.Header + 1);
    Mapped.NamesHeader = (waterfall_names_header*)(Mapped.Files + FileCount);
    Mapped.Names = (uint8*)(Mapped.NamesHeader + 1);
    Mapped.Footer = (waterfall_footer*)(Mapped.Names + Mapped.NamesHeader->Rows * 64);
    Mapped.BestPeers = (waterfall_peer*)(Mapped.Footer + 1);
    Mapped.Peers = Mapped.BestPeers + BEST_PEER_COUNT;

    Mapped.Header->FileCount = FileCount;

    if (UseTmpFile) {
        // TODO(bryce): Check that the file is valid (we are just assuming
        //  that it is for now.
        int32 DebugOutputNo = open(TmpFileName, O_RDWR);
        waterfall OldWaterfall = {};
        struct stat Properties;
        if (stat(OutputFileName, &Properties)) {
            puts("File not found!");
            puts(OutputFileName);
            return 0;
        }
        void* MappingStart = mmap(nullptr, Properties.st_size, PROT_READ, MAP_SHARED,
                                  DebugOutputNo, 0);
        if (OldWaterfall.Header == MAP_FAILED) {
            puts("Something went very wrong with mmap of the backup!");
            return -1;
        }
        close(DebugOutputNo);

        OldWaterfall.Header = (waterfall_header*)MappingStart;
        OldWaterfall.Files = (waterfall_file*)(OldWaterfall.Header + 1);
        OldWaterfall.NamesHeader = (waterfall_names_header*)(OldWaterfall.Files +
                                                             OldWaterfall.Header->FileCount);
        OldWaterfall.Names = (uint8*)(OldWaterfall.NamesHeader + 1);
        OldWaterfall.Footer = (waterfall_footer*)(OldWaterfall.Names + OldWaterfall.NamesHeader->Rows * 64);
        OldWaterfall.BestPeers = (waterfall_peer*)(OldWaterfall.Footer + 1);
        OldWaterfall.Peers = OldWaterfall.BestPeers + BEST_PEER_COUNT;

        // STUDY(bryce): For now, we will be using a simple linear search which will cause a
        //  n^2 cost for searching through the existing files.
        for (uint32 FileIndex = 0; FileIndex < OldWaterfall.Header->FileCount; FileIndex++) {
            waterfall_file* File = OldWaterfall.Files + FileIndex;
            waterfall_file* NextFile = File;
            uint32 NextIndex = FileIndex;
            for (uint32 UpdatedFileIndex = 0; UpdatedFileIndex < Mapped.Header->FileCount; UpdatedFileIndex++) {
                waterfall_file* UpdatedFile = Mapped.Files + UpdatedFileIndex;
                bool32 Found = true;
                if (!strcmp((c8*)Mapped.Names + UpdatedFile->NameIndex, (c8*)OldWaterfall.Names + File->NameIndex)) {
                    // NOTE(bryce): They are the same
                    waterfall_file* NextUpdated = UpdatedFile;
                    uint32 NextUpdatedIndex = UpdatedFileIndex;
                    while (NextFile->ParentIndex != NextIndex && NextUpdated->ParentIndex != UpdatedFileIndex) {
                        NextFile = OldWaterfall.Files + NextIndex;
                        NextIndex = NextFile->ParentIndex;
                        NextUpdated = Mapped.Files + NextUpdatedIndex;
                        NextUpdatedIndex = NextUpdated->ParentIndex;
                        if (strcmp((c8*)Mapped.Names + NextUpdated->NameIndex,
                                   (c8*)OldWaterfall.Names + NextFile->NameIndex) != 0) {
                            // NOTE(bryce): Strings were not the same
                            Found = false;
                            break;
                        }
                    }

                    if (Found) {
                        if (memcmp(UpdatedFile->Hash, File->Hash, HASH_SIZE) != 0) {
                            // NOTE(bryce): Hashes differ so we need top update the file
                            UpdatedFile->Version++;
                        }

                        break;
                    }
                }
            }
        }
        memcpy(Mapped.Header->Salt, OldWaterfall.Header->Salt, SALT_SIZE);
        crypto_generichash(Mapped.Header->WaterfallHash, HASH_SIZE,
                           (uint8*)Mapped.Header->Name, NAME_SIZE + SALT_SIZE,
                           nullptr, 0);

        memcpy(Mapped.Header->PK, OldWaterfall.Header->PK, PK_SIZE);
        memcpy(Mapped.Header->SK, OldWaterfall.Header->SK, SK_SIZE);
        munmap(MappingStart, Properties.st_size);
    }

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

    if (UseTmpFile) {
        // TODO(bryce): Find a better way to do this!
        remove(TmpFileName);
        fclose(DebugOutput);
    }

    return 0;
}
