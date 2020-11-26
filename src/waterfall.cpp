//
// Created by Bryce Wilson on 11/24/20.
//
/**
 * NOTE(bryce): See README.MD for the file structure
 */

#define HASH_SIZE crypto_generichash_BYTES
#define SIG_SIZE crypto_sign_BYTES
#define SK_SIZE crypto_sign_SECRETKEYBYTES
#define PK_SIZE crypto_sign_PUBLICKEYBYTES
#define NAME_SIZE (64 * 4)
#define SALT_SIZE 64
#define MAGIC_STRING "Waterfall data by Bryce Wilson!!"
// NOTE(bryce): This does not include null terminator which is not included
#define MAGIC_STRING_SIZE 32
#define BEST_PEER_COUNT 5

struct waterfall_header {
    // NOTE(bryce): This must be equal to the string:
    //  "Waterfall data by Bryce Wilson!!"
    uint8 MagicString[MAGIC_STRING_SIZE];
    uint32 VersionMajor;
    uint32 VersionMinor;
    uint32 VersionPatch;
    uint8 Reserved0_[20];

    uint8 Name[NAME_SIZE];
    uint8 Salt[SALT_SIZE];

    uint8 WaterfallHash[HASH_SIZE];
    uint8 PK[PK_SIZE];

    uint32 FileCount;
    uint8 Reserved1_[60];

    // NOTE(bryce): This should always be 64 bytes
    uint8 Signature[SIG_SIZE];

    uint8 SK[SK_SIZE];
};

struct waterfall_file {
    uint8 Name[NAME_SIZE];

    uint64 Size;
    uint64 CreationDate;
    uint64 ModificationDate;
    uint32 PieceSize;
    uint32 Version;
    uint32 Permissions;
    uint8 Reserved0_[28];

    uint8 Hash[HASH_SIZE];
    uint8 ParentHash[HASH_SIZE];
};

struct waterfall_file_arena {
    waterfall_file *Files;
    uint32 Capacity;
    uint32 Count;
};

struct waterfall_footer {
    // NOTE(bryce): This should always be 64 bytes
    uint8 Signature[SIG_SIZE];

    uint32 PeerCount;
    uint8 Reserved0_[60];
};

struct waterfall_peer {
    // NOTE(bryce): This should always be 32 bytes
    uint8 PK[PK_SIZE];
    uint64 ZTAddress;
    int32 Confidence;
    uint8 Reserved0_[20];
};

struct waterfall {
    waterfall_header Header;
    waterfall_file *Files;
    waterfall_footer Footer;
    waterfall_peer BestPeers[BEST_PEER_COUNT];
    waterfall_peer *Peers;
};
