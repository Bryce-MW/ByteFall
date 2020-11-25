# ByteFall
I was inspired to work on this by another project that I help out with occationally. They recently started providing a Torrent of their entire website to lower their hosting costs for people who wanted a copy of the entire thing. The issue is that since Torrents can not be updated, they have to provide a single snapshot of the website. When they want to update it, they will have to create a new Torrent file and start getting peers all over again which will end up fragmenting the Torrents and peers significantly. I would like to do something to attempt to at least partially aleviate this issue. That's why I came up with my new ByteFall protocol which uses Waterfall files (basically just say ByteFall wherever you would say BitTorrent and Waterfall wherever you would say Torrent). I will go over the details of the protocol in this README but the basic idea is that the Waterfall file has an "updates must be signed by" field which has a public key. If a node happens to see a newer version of the Waterfall file signed with the specified public key then it will accept that and attempt to update itself in what I belive to be a relitivly smart way. While I have spent quite a while thinking about the protocol, there are still a number of issues and trade-offs that may not be needed. I am very open to any suggestions, comments, or pull requests. While I am compiling in C++, I will mainly be using C features. I won't go into my ideology but I do like operator overloads and function overloads so that's why I compile in C++ but I won't be using much else.

This software is currently on version alpha 0.0.0 which means that I havent implemented it. Other alpha versions may have incompatibilities between even minor versions. Once I am reasonably satisfied with how everthing works, it will move onto the beta stage where significant testing is done. Then it will move on to the release stage where I will consider it good enough to be used in production and hopefully few breaking changes will happen after that.
# Current issues / notes
* It's hard to find out which block of a large file is wrong
* File is not very futur-proof
* There are major issues if the original host disappears.
* Mitigation of increase of connected components such that some nodes would not have a path to an update or full node?
* Perhaps separate download peers from fountain file distribution peers?
* Cloudflare workers system for distibuting updates. Like if we could have a little centralization, the waterfall signer could sign the new timestamp and say hey, I have a new version, here is where to get it, nodes ask if anyone knows how to find it, if it turns out no one knows, ask directly.
* Might be best to identify originating peer with address rather than pub key.
* Finding a way to host that small amount of centralization in a way that will hopefully never go away or is fault tolerant without bringing the network down if someone says they are doing it but don't is hard. I guess we can do a free tier of some cloud provider and hope that never goes away or uses enough CPU that it stops being free.
* Cloudflare workers has a free tier that allows 100000 requests per day and free workers.dev subdomain. That could be a good start and means that we could pay with donations if it becomes popular but if it dies out, it would still work for free for a long time (hopefully). Not as super robust but should be good enough until someone smart has a better idea.
* Possible use of WHOIS system to find domain of workers in case that needs to be switched at some point.
* I have not yet figured out what the best way to do the actual file transfers.
# Technical Details
## Libraries
I will be using [libzt](https://github.com/zerotier/libzt) for the networking. This avoids issues with NAT and port forwarding and does the required encryption for me. It just solves many problems with few downsides. For the basic HTTP requests that I need to interact with Cloudflare Workers, I will be using [libcurl](https://curl.se/libcurl/) which will also do th Base64 encoding (though it is apparently rather slow at this). I will also need to parse the JSON that is produced from that and create JSON to send to it. [cJSON](https://github.com/DaveGamble/cJSON) seems to be one of the better ways to do this.
## Waterfall File Structure
The file is broken into blocks of 64 Bytes. Note that little-endian systems are requiered. The structure is roughly as follows:
```
+----------------------------------------------------------------+
|Waterfall data by Bryce Wilson!!4BV04BV14BV2Reserved------------| (32 Byte Litteral String, 3 uint32 for version into, 20 bytes reserved)
+----------------------------------------------------------------+
|Null terminated string with the name of the Waterfall. Max 255  |
|bytes, UTF-8 encoded. Always takes up 256 bytes. File invalid if|
|the last byte is not 0.                                         |
|                                                                |
+----------------------------------------------------------------+
|64 Byte salt so that multiple Waterfalls can have the same name |
+----------------------------------------------------------------+
|64 Byte Hash of filename and salt. Must be valid                |
+----------------------------------------------------------------+
|4 Bytes Number of Files. 60 Bytes Reserved.                     |
+----------------------------------------------------------------+
|64 Bytes private key OR 32 Bytes of 0, 32 Bytes public key      |
+----------------------------------------------------------------+
|64 Bytes Signature of header with public key                    |
+----------------------------------------------------------------+

Start of files: (directories are just special files. I haven't fully decided how they will work yet.
+----------------------------------------------------------------+
|Null terminated string with the name of the file. Max 255 bytes |
|UTF-8 encoded. Forward slash not allowed. Windows will need     |
|special considerations to allow 1-1 name conversions. Filenames |
|may also not be one or two dots or nothing.                     |
+----------------------------------------------------------------+
|8BFileSz8BCrDate8BChDate4BPS4BFV4BPMFlags/Reserved--------------| (File Size, Creation Timestamp, Change Timespamp, Piece Size, File Version, Permissions, Flags)
+----------------------------------------------------------------+
|64 Bytes hash of file info and file (multipart)                 |
+----------------------------------------------------------------+
|64 Bytes hash of parent directory                               | (0 if file at root)
+----------------------------------------------------------------+

Footer:
+----------------------------------------------------------------+
|64 Byte signature of files (multipart)                          |
+----------------------------------------------------------------+
|4 Byte number of peers. 60 bytes reserved                       | (Put this in ather file since it is transient?)
+----------------------------------------------------------------+
Five closest (ping) peers and other peers below. If there are not 5, they will just be empty and no other peers.
+----------------------------------------------------------------+
|32 Byte peer public key---------8BZTAddr4BCnReserved------------| (Public key, ZT Address, Signed confidence, Reserved)
+----------------------------------------------------------------+
```
## Workers Protocol
TODO
## Inter-node Protocol
TODO
## Actual code or demos or anything interesting
TODO
