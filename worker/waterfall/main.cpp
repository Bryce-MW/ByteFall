/*
 * TODO(bryce):
 *  * Replace this with C++ compiled to webasm
 *  * Use a better storage method. Perhaps something closer to the binary.
 *    (At least as close as you can get considering the utf-8 requirement)
 *    Just to save space, speed would likely actually be slower in that
 *    case.
 *  * Make the error cases also valid JSON so that they can be parsed
 *    easily
 *  * Add some validation of the ZT Address???
 *  * Consider consolidating consts and variables using a fancy struct
 *
 * NOTE(bryce): So what I am thinking is that the GET request would
 *  look as follows:
 *  https://waterfall.brycemw.workers.dev/?waterfall=TestHash
 *  <-------------------------------------------------------------------------------------->
 *  I will suggest that the following will be response for a GET
 *  {
 *    Found: true,
 *    Result : {
 *      Waterfall: "The hash repeated",
 *      Address: "ZT Addr",
 *      Timestamp: unix timestamp,
 *      Signature: "Signature by the creator",
 *      Key: "Public key used for signature"
 *    }
 *  }
 *  <-------------------------------------------------------------------------------------->
 *  I will also suggest the following for a PUT request
 *  https://waterfall.brycemw.workers.dev/?waterfall=TestHash2&address=ZTAddr&timestamp=101&signature=SomeSig&key=TheKey
 *  <-------------------------------------------------------------------------------------->
 *  Result:
 *  {
 *    New: true,
 *    Updated: false,
 *    Result : {
 *      Waterfall: "The hash repeated",
 *      Address: "ZT Addr", // These will have to be Base 64 encoded
 *      Timestamp: unix timestamp, // These will have to be Base 64 encoded
 *      Signature: "Signature by the creator",
 *      Key: "Public key used for signature"
 *    }
 *  }
 *
 */

/*
 * TODO(bryce): Consider just making these globals and using malloc for actual memory alloc
 * NOTE(bryce): Heap memory starts at __heap_base. The layout is as follows:
 *  HeapBase +   0: Signature, 64 bytes
 *  HeapBase +  64: Waterfall Hash, 32 bytes
 *  HeapBase +  96: Public Key, 32 bytes
 *  HeapBase + 128: Address, 8 bytes
 *  HeapBase + 136: Timestamp, 8 bytes
 *  <->
 *  HeapBase + 144: Spacer before new: 112 bytes
 *  <->
 *  HeapBase + 256: Signature, 64 bytes
 *  HeapBase + 320: Waterfall Hash, 32 bytes
 *  HeapBase + 352: Public Key, 32 bytes
 *  HeapBase + 384: Address, 8 bytes
 *  HeapBase + 392: Timestamp, 8 bytes
 */

#include <stdint.h>
#include <sodium.h>

#define export __attribute__((visibility("default"))) extern "C"
#define import extern "C"
#define internal __attribute__((internal_linkage))

typedef int32_t i32;
typedef uint32_t u32;
typedef int64_t i64;
typedef float f32;
typedef double f64;

typedef uint8_t u8;
typedef uint64_t u64;

extern volatile u8 __heap_base;
export volatile u8* HeapBase = &__heap_base;

/**
 * JS function to get the JS object containing the search params.
 * @param ScopeID Scope ID to grab JS objects
 * @param RequestID ID of JS Request object
 * @return ID of a searchParams object
 */
import u32 GetSearchParams(u32 ScopeID, u32 RequestID);
/**
 * Gets a specific param by a JS string which is stored in the const array
 * @param ScopeID Scope ID to grab JS objects
 * @param SearchParamsID ID of searchParams object
 * @param Param The param number to grab
 * @return The ID of the string param result (could be null)
 */
import u32 GetParam(u32 ScopeID, u32 SearchParamsID, u32 Const);
/**
 * Just grabs a JS object and returns if it is null
 * @param ScopeID Scope ID to grab the JS objects
 * @param ObjectID the ID of the object to check
 * @return 1 if the object is null and 0 otherwise
 */
import u32 IsNull(u32 ScopeID, u32 ObjectID);
/**
 * Gets the KV data in arrayBuffer format for the given key
 * @param ScopeID Scope ID to grab the JS objects
 * @param KeyID The ID of the key string
 * @return The ID of the arrayBuffer
 */
import u32 GetKV(u32 ScopeID, u32 KeyID);
/**
 * Prints the given JS object
 * @param ScopeID Scope ID to grab the JS objects
 * @param ObjectID ID of the object to print
 */
import void PrintJS(u32 ScopeID, u32 ObjectID);
/**
 * Get an attribute by a constant string
 * @param ScopeID Scope ID to grab the JS objects
 * @param ObjectID ID of the object to get the attribute of
 * @param Const Number of the constant to use
 * @return ID of the returned object
 */
import u32 GetAttribute(u32 ScopeID, u32 ObjectID, u32 Const);
/**
 * Set an attribute
 * @param ScopeID Scope ID of the object to set
 * @param ObjectID ID of the object to set
 * @param Const Const ID of the param name
 * @param NewScopeID Scope ID of the object to set to (1 if const)
 * @param NewObjectID Object ID of the object to set to
 */
import void SetAttribute(u32 ScopeID, u32 ObjectID, u32 Const, u32 NewScopeID, u32 NewObjectID);
/**
 * Uses the === operator between the given object and the constant
 * @param ScopeID Scope ID to grab the JS objects
 * @param ObjectID ID of the object to check equality
 * @param Const Number of the constant to use
 * @return 1 if true else 0
 */
import u32 EqConst(u32 ScopeID, u32 ObjectID, u32 Const);
/**
 * Creates a new object
 * @param ScopeID the ID of the scope to put the object in
 */
import u32 NewObject(u32 ScopeID);
/**
 * Copies a JS ArrayBuffer into wasm memory at the given address. This is unchecked so it
 *  could crash
 * @param ScopeID Scope ID of the object
 * @param ObjectID ID of the object to be copied in
 * @param Address The address where the object should be copied to
 * @param Length The amount of data to copy
 */
import void CopyToMemory(u32 ScopeID, u32 ObjectID, void* Address, u32 Length);
/**
 * Creates a response object by turning the given object to JSON and adding the response code
 * @param ScopeID Scope ID of the object
 * @param ObjectID ID of the object
 * @param ResponseCode the HTTP response code for the response
 * @return the ID of the new response object
 */
import u32 CreateResponse(u32 ScopeID, u32 ObjectID, u32 ResponseCode);
/**
 * Encodes memory into a Base64 JS string
 * @param ScopeID Scope ID for the resulting object
 * @param Address Where in memory to start the encoding
 * @param Length The length to encode
 * @return the ID of the JS string object
 */
import u32 Base64Encode(u32 ScopeID, void* Address, u32 Length);


struct waterfall_value {
    u8 Sig[64];
    u8 Hash[32];
    u8 PK[32];
    u64 ZT;
    u64 Timestamp;
};

static volatile waterfall_value* StoredValue = (waterfall_value*)HeapBase;
static volatile waterfall_value* NewValue = (waterfall_value*)(HeapBase + 256);

export u32 Init() {
    return sodium_init();
}

internal u32 memeq(u8* X, u8* Y, u64 Size) {
    while (Size--) {
        if (*(X + Size) != *(Y + Size)) {
            return false;
        }
    }
    return true;
}

export u32 UpdateAllowed() {
    return (NewValue->Timestamp > StoredValue->Timestamp) &&
           (memeq((u8*)StoredValue->PK, (u8*)NewValue->PK, 32)) &&
           (crypto_sign_verify_detached((u8*)NewValue->Sig,
                                        (u8*)NewValue->Hash,
                                        sizeof(waterfall_value) - 64,
                                        (u8*)NewValue->PK) == 0);
}

export u32 SignatureValid() {
    return crypto_sign_verify_detached((u8*)NewValue->Sig,
                                       (u8*)NewValue->Hash,
                                       sizeof(waterfall_value) - 64,
                                       (u8*)NewValue->PK) == 0;
}

export u32 HandleRequest(u32 ScopeID, u32 RequestID) {
    u32 SearchParamsID = GetSearchParams(ScopeID, RequestID);
    u32 WaterfallHashStringID = GetParam(ScopeID, SearchParamsID, 0);
    u32 WaterfallID = GetKV(ScopeID, WaterfallHashStringID);

    int MethodID = GetAttribute(ScopeID, RequestID, 2);

    if (EqConst(ScopeID, MethodID, 4)) {
        // NOTE(bryce): Method is GET
        i32 Result = NewObject(ScopeID);
        SetAttribute(ScopeID, Result, 6, 1, 14);
        i32 RResult = NewObject(ScopeID);
        SetAttribute(ScopeID, RResult, 8, ScopeID, WaterfallHashStringID);
        SetAttribute(ScopeID, RResult, 9, 1, 15);
        SetAttribute(ScopeID, RResult, 10, 1, 15);
        SetAttribute(ScopeID, RResult, 11, 1, 15);
        SetAttribute(ScopeID, RResult, 12, 1, 15);
        SetAttribute(ScopeID, Result, 7, ScopeID, RResult);

        if (IsNull(ScopeID, WaterfallID)) {
            return CreateResponse(ScopeID, Result, 404);
        }

        CopyToMemory(ScopeID, WaterfallID, (u8*)HeapBase, sizeof(waterfall_value));
        waterfall_value* Waterfall = (waterfall_value*)HeapBase;

        u32 AddressID = Base64Encode(ScopeID, &Waterfall->ZT, sizeof(Waterfall->ZT));
        u32 TimestampID = Base64Encode(ScopeID, &Waterfall->Timestamp, sizeof(Waterfall->Timestamp));
        u32 SignatureID = Base64Encode(ScopeID, &Waterfall->Sig, sizeof(Waterfall->Sig));
        u32 KeyID = Base64Encode(ScopeID, &Waterfall->PK, sizeof(Waterfall->PK));

        SetAttribute(ScopeID, Result, 6, 1, 13);
        SetAttribute(ScopeID, RResult, 9, ScopeID, AddressID);
        SetAttribute(ScopeID, RResult, 10, ScopeID, TimestampID);
        SetAttribute(ScopeID, RResult, 11, ScopeID, SignatureID);
        SetAttribute(ScopeID, RResult, 12, ScopeID, KeyID);
        SetAttribute(ScopeID, Result, 7, ScopeID, RResult);

        return CreateResponse(ScopeID, Result, 200);
    } else if (EqConst(RequestID, MethodID, 3)) {
        // NOTE(bryce): Method is POST
    } else {
        return CreateResponse(1, 5, 405);
    }

    return CreateResponse(1, 1, 501);
}

/**
let Consts = [
  'waterfall',
  "Invalid code path",
  "method",
  "POST",
  "GET",
  "Unexpected method",
  "Found",
  "Result",
  "Waterfall",
  "Address",
  "Timestamp",
  "Signature",
  "Key",
  true,
  false,
  null
];
 */
