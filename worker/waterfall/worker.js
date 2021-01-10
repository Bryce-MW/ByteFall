const Wasm = new WebAssembly.Instance(WATERFALL_WASM, {
  env: {
    "GetSearchParams": GetSearchParams,
    "GetParam": GetParam,
    "IsNull": IsNull,
    "GetKV": GetKV,
    "PrintJS": PrintJS,
    "GetAttribute": GetAttribute,
    "EqConst": EqConst,
    "SetAttribute": SetAttribute,
    "NewObject": NewObject,
    "CopyToMemory": CopyToMemory,
    "CreateResponse": CreateResponse,
    "Base64Encode": Base64Encode
  }
});
const WasmExports = Wasm.exports;
const HandleRequest = WasmExports.HandleRequest;
const UpdateAllowed = WasmExports.UpdateAllowed;
const SignatureValid = WasmExports.SignatureValid;
const WasmMemory = WasmExports.memory;
const HeapBase = WasmExports.HeapBase;

if (WasmExports.Init < 0) {
  // NOTE(bryce): We basically just want it to crash and I think this is the easiest way to do that.
  let x = null.something;
}

let JSStore = {};
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


function GetSearchParams(ScopeID, RequestID) {
  let Request = JSStore[ScopeID][RequestID];
  const {searchParams} = new URL(Request.url);
  return JSStore[ScopeID].push(searchParams) - 1;
}

function GetParam(ScopeID, SearchParamsID, Const) {
  let searchParams = JSStore[ScopeID][SearchParamsID];
  let Result = searchParams.get(Consts[Const]);
  return JSStore[ScopeID].push(Result) - 1;
}

function IsNull(ScopeID, ObjectID) {
  return JSStore[ScopeID][ObjectID] === null ? 1 : 0;
}

async function GetKV(ScopeID, KeyID) {
  let Key = JSStore[ScopeID][KeyID];
  let Value = await WF_KV.get(Key);
  console.log("Key:", Key);
  console.log("Val:", Value);
  return JSStore[ScopeID].push(Value) - 1;
}

function PrintJS(ScopeID, ObjectID) {
  console.log(JSStore[ScopeID][ObjectID]);
}

function GetAttribute(ScopeID, ObjectID, Const) {
  let Attribute = Consts[Const];
  let Result = JSStore[ScopeID][ObjectID][Attribute];
  return JSStore[ScopeID].push(Result) - 1;
}

function EqConst(ScopeID, ObjectID, Const) {
  let Left = JSStore[ScopeID][ObjectID];
  let Right = Consts[Const];
  return Left === Right ? 1 : 0;
}

function SetAttribute(ScopeID, ObjectID, Const, NewScopeID, NewObjectID) {
  let Left = JSStore[ScopeID][ObjectID];
  let Attribute = Consts[Const];
  let Right;
  if (NewScopeID === 1) {
    Right = Consts[NewObjectID];
  } else {
    Right = JSStore[NewScopeID][NewObjectID];
  }
  Left[Attribute] = Right;
}

function NewObject(ScopeID) {
  return JSStore[ScopeID].push({}) - 1;
}

function CopyToMemory(ScopeID, ObjectID, Address, Length) {
  // NOTE(bryce): This assumes that the given object is actually an ArrayBuffer and is UB otherwise
  let Buffer = new Uint32Array(JSStore[ScopeID][ObjectID]);
  let Memory = new Uint32Array(WasmMemory.buffer, Address, Length)
  Memory.set(Buffer);
}

function CreateResponse(ScopeID, ObjectID, ResponseCode) {
  let JSONResponse;
  if (ScopeID === 1) {
    JSONResponse = Consts[ObjectID];
  } else {
    JSONResponse = JSON.stringify(JSStore[ScopeID][ObjectID]);
  }
  let OurResponse = new Response(JSONResponse, {status: ResponseCode});
  return JSStore[ScopeID].push(OurResponse) - 1;
}

function Base64Encode(ScopeID, Address, Length) {
  let Buffer = new Uint8Array(WasmMemory.buffer, Address, Length);
  let Encoded = btoa(String.fromCharCode(...Buffer));
  return JSStore[ScopeID].push(Encoded) - 1;
}

function Base64Decode(Object, Address) {
  // NOTE(bryce): I am just setting a sane limit for now but it will likely need to be set
  //  dynamically in the future.
  let Memory = new Uint8Array(WasmMemory.buffer, Address, 256);
  Memory.set(Uint8Array.from(atob(Object), c => c.charCodeAt(0)));
}

function TransferNewData(Waterfall) {
  Base64Decode(Waterfall.Signature, HeapBase + 0)
  Base64Decode(Waterfall.Waterfall, HeapBase + 64)
  Base64Decode(Waterfall.Key, HeapBase + 96)
  Base64Decode(Waterfall.Address, HeapBase + 128)
  Base64Decode(Waterfall.Timestamp, HeapBase + 136)
}

function TransferOldData(Waterfall) {
  Base64Decode(Waterfall.Signature, HeapBase + 256 + 0)
  Base64Decode(Waterfall.Waterfall, HeapBase + 256 + 64)
  Base64Decode(Waterfall.Key, HeapBase + 256 + 96)
  Base64Decode(Waterfall.Address, HeapBase + 256 + 128)
  Base64Decode(Waterfall.Timestamp, HeapBase + 256 + 136)
}

addEventListener('fetch', event => {
  // let ScopeID = Math.floor(Math.random() * 1000000000) + 2;
  // JSStore[ScopeID] = [];
  // let RequestID = JSStore[ScopeID].push(event.request) - 1;
  // let ResultID = HandleRequest(ScopeID, RequestID);
  // let Result = JSStore[ScopeID][ResultID];
  // delete JSStore[ScopeID];
  // event.respondWith(Result)
  event.respondWith(handleRequest(event.request));
})

/**
 * Respond to the request
 * @param {Request} Request
 */
async function handleRequest(Request) {
  const { searchParams } = new URL(Request.url);
  let WaterfallHash = searchParams.get('waterfall');
  let OurResponse = new Response("Invalid code path", {status:501});

  if (WaterfallHash === null) {
    OurResponse = new Response("No Data given", {status:400});
  } else {
    const Waterfall = await WF_KV.get(WaterfallHash, "json");
    if (Request.method === "POST") {
      // NOTE(bryce): We want to add or update the value

      let Address = searchParams.get("address");
      let Timestamp = searchParams.get("timestamp");
      let Signature = searchParams.get("signature");
      let Key = searchParams.get("key");
      if (Address === null || Timestamp === null || Signature === null ||
          Key === null || Timestamp > (Date.now() / 1000)) {
        console.log(Address, Timestamp, Signature, Key, Timestamp, (Date.now() / 1000));
        OurResponse = new Response("Not enough data given", {status: 400});
      } else {
        // TODO(bryce): Do some data validation
        let Entry = {
          Address: Address,
          Timestamp: Timestamp,
          Signature: Signature,
          Key: Key
        }
        console.log(Entry);
        let Result = {
          New: false,
          Updated: false,
          // TODO(bryce): Put in the actual value
          Result: null
        };
        if (Waterfall === null) {
          // NOTE(bryce): Create a new entry
          TransferNewData(Waterfall);
          if (SignatureValid() === 1) {
            WF_KV.put(WaterfallHash, JSON.stringify(Entry));
            Result.New = true;
            Result.Result = Entry;
            Result.Result.Waterfall = WaterfallHash;
            OurResponse = new Response(JSON.stringify(Result));
          } else {
            OurResponse = new Response(JSON.stringify(Result));
          }
        } else {
          // NOTE(bryce): Attempt to update an entry
          Waterfall.Waterfall = WaterfallHash;
          Entry.Waterfall = WaterfallHash;
          // TODO(bryce): Check the key, signature, and timestamp
          //  With the timestamp, it needs to be before now!
          TransferNewData(Waterfall);
          TransferOldData(Entry);

          if (UpdateAllowed() === 1) {
            WF_KV.put(WaterfallHash, JSON.stringify(Entry));
            Result.Updated = true;
            Result.Result = Entry;
            OurResponse = new Response(JSON.stringify(Result));
          } else {
            OurResponse = new Response(JSON.stringify(Result));
          }
        }
      }
    } else if (Request.method === "GET") {
      // NOTE(bryce): We want to get the value
      let Result = {
        Found: false,
        Result: {
          Waterfall: WaterfallHash,
          Address: null,
          Timestamp: null,
          Signature: null,
          Key: null
        }
      };
      if (Waterfall === null) {
        OurResponse = new Response(JSON.stringify(Result));
      } else {
        Result.Found = true;
        Result.Result = Waterfall;
        Result.Result.Waterfall = WaterfallHash;

        OurResponse = new Response(JSON.stringify(Result));
      }
      console.log(Result);
    } else {
      OurResponse = new Response("Unexpected method", {status:405});
    }
  }
  return OurResponse;
}
