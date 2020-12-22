addEventListener('fetch', event => {
  event.respondWith(handleRequest(event.request))
})

/**
 * Respond to the request
 * @param {Request} request
 */
async function handleRequest(Request) {
  let test = TEST_WASM;

  const { searchParams } = new URL(Request.url);
  let WaterfallHash = searchParams.get('waterfall');
  const Waterfall = await Waterfalls.get(WaterfallHash, "json");
  console.log(WaterfallHash);
  let OurResponse = new Response("Invalid code path", {status:501});

  if (WaterfallHash === null) {
    OurResponse = new Response("No Data given", {status:400});
  } else {
    if (Request.method === "POST") {
      // NOTE(bryce): We want to add or update the value

      let Address = searchParams.get("address");
      let Timestamp = searchParams.get("timestamp");
      let Signature = searchParams.get("signature");
      let Key = searchParams.get("key");
      if (Address === null || Timestamp === null || Signature === null ||
          Key === null) {
        OurResponse = new Response("Not enough data given", {status:400});
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
          Result: Waterfall
        };
        if (Waterfall === null) {
          // NOTE(bryce): Create a new entry
          Waterfalls.put(WaterfallHash, JSON.stringify(Entry));
          Result.New = true;
          Result.Result = Entry;
          Result.Result.Waterfall = WaterfallHash;
          OurResponse = new Response(JSON.stringify(Result));
        } else {
          // NOTE(bryce): Attempt to update an entry
          // TODO(bryce): Check the key, signature, and timestamp
          //  With the timstamp, it needs to be before now!
          Result.Result.Waterfall = WaterfallHash;
          if (Waterfall.Timestamp >= Timestamp) {
            OurResponse = new Response(JSON.stringify(Result));
          } else {
            Waterfalls.put(WaterfallHash, JSON.stringify(Entry));
            Result.Updated = true;
            Result.Result = Entry;
            Result.Result.Waterfall = WaterfallHash;
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
 *
 * NOTE(bryce): So what I am thinking is that the GET request would
 * look as follows:
 * https://waterfall.brycemw.workers.dev/?waterfall=TestHash
 *
 * I will suggest that the following will be response for a GET
 * {
 *   Found: true,
 *   Result : {
 *     Waterfall: "The hash repeated",
 *     Address: "ZT Addr",
 *     Timestamp: unixtimestamp,
 *     Signature: "Signature by the creator",
 *     Key: "Public key used for signature"
 *   }
 * }
 *
 * I will also suggest the following for a PUT request
 * https://waterfall.brycemw.workers.dev/?waterfall=TestHash2&address=ZTAddr&timestamp=101&signature=SomeSig&key=TheKey
 *
 * Result:
 * {
 *   New: true,
 *   Updated: false,
 *   Result : {
 *     Waterfall: "The hash repeated",
 *     Address: "ZT Addr",
 *     Timestamp: unixtimestamp,
 *     Signature: "Signature by the creator",
 *     Key: "Public key used for signature"
 *   }
 * }
 *
 */
