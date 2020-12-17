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

struct simple_u32_dynamic_array {
    uint32 count;
    uint32 max;
    uint32* array;
};

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

#if 0
    int32 DebugOutputNo = open(OutputFileName, O_RDWR);
    waterfall Mapped = {};
    struct stat Properties;
    if (stat(OutputFileName, &Properties)) {
        puts("File not found!");
        puts(OutputFileName);
        return 0;
    }

    void* MappingStart = mmap(nullptr, Properties.st_size, PROT_READ, MAP_SHARED,
                              DebugOutputNo, 0);
    if (Mapped.Header == MAP_FAILED) {
        puts("Something went very wrong with mmap!");
        return -1;
    }
    close(DebugOutputNo);

    Mapped.Header = (waterfall_header*)MappingStart;
    Mapped.Files = (waterfall_file*)(Mapped.Header + 1);
    Mapped.NamesHeader = (waterfall_names_header*)(Mapped.Files +
            Mapped.Header->FileCount);
    Mapped.Names = (uint8*)(Mapped.NamesHeader + 1);
    Mapped.Footer = (waterfall_footer*)(Mapped.Names + Mapped.NamesHeader->Rows * 64);
    Mapped.BestPeers = (waterfall_peer*)(Mapped.Footer + 1);
    Mapped.Peers = Mapped.BestPeers + BEST_PEER_COUNT;

    // TODO(bryce): Find a \/ way to resolve this error
    if ((uint64)Properties.st_size < sizeof(waterfall_header) +
                                     sizeof(waterfall_names_header) +
                                     sizeof(waterfall_footer) +
                                     sizeof(waterfall_peer)*BEST_PEER_COUNT) {
        puts("Size is not large enough for headers");
        return -1;
    }

    if ((uint64)Properties.st_size < sizeof(waterfall_header) +
                                     sizeof(waterfall_names_header) +
                                     sizeof(waterfall_footer) +
                                     sizeof(waterfall_peer)*BEST_PEER_COUNT +
                                     sizeof(waterfall_file)*Mapped.Header->FileCount) {
        puts("Size is not large enough for files");
        return -1;
    }

    if ((uint64)Properties.st_size < sizeof(waterfall_header) +
                                     sizeof(waterfall_file)*Mapped.Header->FileCount +
                                     sizeof(waterfall_names_header) +
                                     Mapped.NamesHeader->Rows*64 +
                                     sizeof(waterfall_footer) +
                                     sizeof(waterfall_peer)*BEST_PEER_COUNT) {
        puts("Size is not large enough for names");
        return -1;
    }

    if (memcmp(Mapped.Header->MagicString, MAGIC_STRING, MAGIC_STRING_SIZE) != 0) {
        puts("Magic string is not correct");
        return -1;
    }

    // TODO(bryce): Check the version and use that to enable/disable features.

    uint8 ActualWaterfallHash[HASH_SIZE];
    crypto_generichash(ActualWaterfallHash, HASH_SIZE,
                       (uint8*)Mapped.Header->Name, NAME_SIZE + SALT_SIZE,
                       nullptr, 0);
    if (memcmp(ActualWaterfallHash, Mapped.Header->WaterfallHash, HASH_SIZE) != 0) {
        puts("Waterfall Hash does not match");
        return -1;
    }

    if (crypto_sign_verify_detached(Mapped.Header->Signature, (uint8*)Mapped.Header,
                                    (uint8*)&Mapped.Header->FileCount -
                                    (uint8*)Mapped.Header,
                                    Mapped.Header->PK) != 0) {
        puts("Header signature is not correct");
        return -1;
    }

    bool32 Empty = true;
    for (uint32 I = 0; (I < (SK_SIZE/8)) && Empty; ++I) {
        if (((uint64*)Mapped.Header->SK)[I]) {
            Empty = false;
        }
    }
    if (Empty) {
        puts("Currently, this program is only used to update secret waterfalls");
        return -1;
    }

    if (crypto_sign_verify_detached(Mapped.Footer->Signature, (uint8*)Mapped.Files,
                                    sizeof(waterfall_file) *
                                    Mapped.Header->FileCount +
                                    sizeof(waterfall_names_header) +
                                    Mapped.NamesHeader->Rows * 64,
                                    Mapped.Header->PK) != 0) {
        puts("Header signature is not correct");
        return -1;
    }

    // TODO(bryce): Check peers

    // TODO(bryce): Check the files
    // STUDY(bryce): For now, we will be using a simple linear search which will cause a
    //  n^2 cost for searching through the existing files.
    // NOTE(bryce): So what I am thinking is to go through all of the files, check if
    //  if they exist, need updating, etc. I will mark non-existent ones as being free
    //  to use again. Then I will go through the new files and fill in the holes as I
    //  can. When that is full and I need more space, I will use the mem remap thing to
    //  expand the mapping as needed. I'll also have to copy the footer and peers if I
    //  need to do this. I think that I should also rebuild the names cache. This whole
    //  process needs to be better thought about since it seems to be rather bad in
    //  terms of memory space and time complexity.

    simple_u32_dynamic_array ExtraSlots = {};
    ExtraSlots.max = 8;
    ExtraSlots.array = (uint32*)malloc(4*8);

    // TODO(bryce): Rebuild the name cache so that we can remove names for nonexistent
    //  files
    c8* Path = nullptr;
    // TODO(bryce): This could end up with a smaller size than the rows so it will need
    //  to be shrunk before writing
    auto* NewNameCache = (c8*)malloc(Mapped.NamesHeader->Rows*64);
    uint32 NewNameCacheSize = 0;
    uint32 NewNameCacheRows = Mapped.NamesHeader->Rows;
    for (uint32 FileIndex = 0; FileIndex < Mapped.Header->FileCount; FileIndex++) {
        waterfall_file* File = &Mapped.Files[FileIndex];
        waterfall_file* NextFile = File;
        uint32 NextIndex = FileIndex;
        uint64 PathSize = strlen((c8*)&Mapped.Names[File->NameIndex]) + 1;
        Path = (c8*)realloc(Path, PathSize);
        strcpy(Path, (c8*)&Mapped.Names[File->NameIndex]);
        while(NextFile->ParentIndex != NextIndex) {
            NextFile = &Mapped.Files[NextIndex];
            NextIndex = NextFile->ParentIndex;
            uint64 NewNameSize = strlen((c8*)&Mapped.Names[NextFile->NameIndex]) + 1;
            PathSize += NewNameSize;
            Path = (c8*)realloc(Path, PathSize);
            memmove(Path + NewNameSize, Path, PathSize);
            memcpy(Path, (c8*)&Mapped.Names[NextFile->NameIndex], NewNameSize);
            Path[NewNameSize - 1] = '/';
        }

        struct stat SystemFileStat;
        if (stat(Path, &SystemFileStat)) {
            // NOTE(bryce): stat returns -1 if an error occurs, 0 otherwise
            if (ExtraSlots.count == ExtraSlots.max) {
                ExtraSlots.max *= 2;
                ExtraSlots.array = (uint32*)realloc(ExtraSlots.array,
                                                    4*ExtraSlots.max);
            }
            ExtraSlots.array[ExtraSlots.count++] = FileIndex;
        } else {
            // NOTE(bryce): File was found
            if (NewNameCacheSize == NewNameCacheRows*64) {
                // TODO(bryce): Ensure that the new size can fit the new name
                NewNameCache = (c8*)realloc(NewNameCache, ++NewNameCacheRows*64);
            }
            uint64 NameLength = strlen((c8*)&Mapped.Names[File->NameIndex]) + 1;
            memcpy(&NewNameCache[NewNameCacheSize], &Mapped.Names[File->NameIndex],
                   NameLength);
            File->NameIndex = NewNameCacheSize;
            NewNameCacheSize += NameLength;
            // TODO(bryce): Figure out why off_t is signed
            if ((uint64)SystemFileStat.st_size != File->Size ||
                SystemFileStat.st_mode != File->Permissions) {
                // NOTE(bryce): We don't even need to compare hashes
                uint32 CurrentVersion = File->Version;
                file_type SystemFileType = GetFileType(&SystemFileStat);
                FillFileProperties(File, &SystemFileStat, SystemFileType);
                File->Version = CurrentVersion + 1;
                HashFile(File, SystemFileType, Path, (c8*)Mapped.Names,
                         strlen((c8*)&Mapped.Names[File->NameIndex]));
            } else {
                // NOTE(bryce): We must rehash and compare
                uint8 OldHash[HASH_SIZE];
                memcpy(OldHash, File->Hash, HASH_SIZE);
                file_type SystemFileType = GetFileType(&SystemFileStat);
                HashFile(File, SystemFileType, Path, (c8*)Mapped.Names,
                         strlen((c8*)&Mapped.Names[File->NameIndex]));
                if (memcmp(OldHash, File->Hash, HASH_SIZE) != 0) {
                    uint32 CurrentVersion = File->Version;
                    FillFileProperties(File, &SystemFileStat, SystemFileType);
                    File->Version = CurrentVersion + 1;
                }
            }
        }
    }
    free(Path);

    Mapped.Header->FileCount -= ExtraSlots.count;

    /*
     * NOTE(bryce): Let's think about what we are going to do here. What I want to do is
     *  √ Copy the footer and best peers to a temporary location.
     *  * Then I can go back and loop through the files
     *   * Check if they exist already, and
     *   * otherwise slot them into an open area.
     *    * If we run out, do an unmap.
     *    * Check if existent first.
     *  * Then, I need to check if there is space left, if so,
     *   * defrag in a simple way, and
     *   * copy everything back in as needed
     *   * including the required shrinking of the file.
     *  * If there are no holes and no extra space used,
     *   * shrink size back to what it was
     *   * and update everything as needed.
     *  * Remember to check the size of the new names section as well.
     *  * If more space was used,
     *   * then we need to calculate the new file size and
     *   * copy everything back in as needed.
     */

    waterfall_footer StoredFooter;
    memcpy(&StoredFooter, Mapped.Footer, sizeof(waterfall_footer));

    waterfall_peer StoredBestPeers[BEST_PEER_COUNT];
    memcpy(&StoredBestPeers, Mapped.BestPeers,
           sizeof(waterfall_peer)*BEST_PEER_COUNT);



    if (MappingStart) {
        munmap(Mapped.Header, Properties.st_size);
    }
#endif
    waterfall_header Header = {};
    memcpy(Header.MagicString, MAGIC_STRING, 32);
    Header.VersionMajor = VERSION_MAJOR;
    Header.VersionMinor = VERSION_MINOR;
    Header.VersionPatch = VERSION_PATCH;
    strcpy(Header.Name, SourceFileName);
    randombytes_buf(&Header.Salt, SALT_SIZE);
    crypto_generichash(Header.WaterfallHash, HASH_SIZE,
                       (uint8*)Header.Name, NAME_SIZE + SALT_SIZE,
                       nullptr, 0);
    crypto_sign_keypair(Header.PK, Header.SK);

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
    DebugOutput = fopen(OutputFileName, "wb");
    fwrite(&Header, sizeof(waterfall_header), 1, DebugOutput);

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


    if (UseTmpFile) {
        // TODO(bryce): Check existing files
    }

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
