# ByteFall
I was inspired to work on this by another project that I help out with occasionally. They recently started providing a
Torrent of their entire website to lower their hosting costs for people who wanted a copy of the entire thing. The issue
is that since Torrents can not be updated, they have to provide a single snapshot of the website. When they want to
update it, they will have to create a new Torrent file and start getting peers all over again which will end up
fragmenting the Torrents and peers significantly. I would like to do something to attempt to at least partially
alleviate this issue. That's why I came up with my new ByteFall protocol which uses Waterfall files (basically just say
ByteFall wherever you would say BitTorrent and Waterfall wherever you would say Torrent). I will go over the details of
the protocol in this README but the basic idea is that the Waterfall file has an "updates must be signed by" field which
has a public key. If a node happens to see a newer version of the Waterfall file signed with the specified public key
then it will accept that and attempt to update itself in what I believe to be a relatively smart way. While I have spent
quite a while thinking about the protocol, there are still a number of issues and trade-offs that may not be needed. I
am very open to any suggestions, comments, or pull requests. This repo used to be written in C++ but I have decided to
switch to Rust for version 0.2.0. This is my first time using Rust so I may not use it perfectly. I will probably write
it in a style similar to C which seems to be compatible with the ideology of Rust with the bonus of not falling into
some of the mature pitfalls that are all to easy to fall into in C.

This software is currently on version alpha 0.2.0 which means that I haven't implemented it. Other alpha versions may
have incompatibilities between even minor versions. Once I am reasonably satisfied with how everything works, it will
move onto the beta stage where significant testing is done. Then it will move on to the release stage where I will
consider it good enough to be used in production and hopefully few breaking changes will happen after that.

# Current issues / notes

* It's hard to find out which block of a large file is wrong

* File is not very future-proof

* There are major issues if the original host disappears.

* Mitigation of increase of connected components such that some nodes would not have a path to an update or full node?

* Perhaps separate download peers from fountain file distribution peers?

* Cloudflare workers system for distributing updates. Like if we could have a little centralization, the waterfall
  signer could sign the new timestamp and say hey, I have a new version, here is where to get it, nodes ask if anyone
  knows how to find it, if it turns out no one knows, ask directly.

* Finding a way to host that small amount of centralization in a way that will hopefully never go away or is fault
  tolerant without bringing the network down if someone says they are doing it but don't is hard. I guess we can do a
  free tier of some cloud provider and hope that never goes away or uses enough CPU that it stops being free.

* Cloudflare workers has a free tier that allows 100000 requests per day and free workers.dev subdomain. That could be a
  good start and means that we could pay with donations if it becomes popular but if it dies out, it would still work
  for free for a long time (hopefully). Not as super robust but should be good enough until someone smart has a better
  idea.

* Possible use of WHOIS system to find domain of workers in case that needs to be switched at some point.

* I have not yet figured out what the best way to do the actual file transfers.

* The files should be listed in a hash table format so that searching for any specific one by their hash is easy to do.

* Add something to the header to show what version of the Waterfall we are at.

* Reject more bad filenames like those that include control characters. Note that Unicode is like way more unsafe than
  you might want but we can try our best. Maybe make a crate that others can use.

* For the 1.0 release, maybe make it so that everyone has the full route table thing. But make it clear that this will
  change in the 2.0 release so you must be sure to update when that comes out.

## Additional notes found in the old C++ files

* Handle errors properly
* Pull waterfall_file creation out into a function
* Add CF communication
* Learn more about proper memory management to ensure that I am doing everything the the correct way to get the best
  performance
* Possibly add a version tag to the header to quickly see if this is really a new version. I am not sure that I will
  need it though so I am holding off until I start working on the protocol.


* Get rid of struct stat and get the properties directly from the path in a platform agnostic way
* Find a good way to get creation date if available and set to 0 otherwise
* Decide on a better form of permissions. Perhaps only supporting the basics of whether the execute bit is on or not.
  Then synthesise it on Windows. There is just so much space unused that I wasn't sure what to do with it all.

# Technical Details
## Libraries
My original plan was to use [libzt](https://github.com/zerotier/libzt) for networking and
[libcurl](https://curl.se/libcurl/) for interacting with Workers. Now that I am using Rust, there will likely be a built
in way to do the things that I was going to do in libcurl but I will likely have to bind directly with libzt but I will
get to that when it comes to that part of the code.

## Waterfall File Structure
The file is broken into blocks of 64 Bytes. Note that little-endian systems are required. The structure is roughly as
follows:
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
|32B Hash name and salt----------32B Public Key------------------|
+----------------------------------------------------------------+
|4 Bytes Number of Files. 60 Bytes Reserved.                     |
+----------------------------------------------------------------+
|64 Bytes Signature of header with public key                    |
+----------------------------------------------------------------+
|64 Bytes private key OR empty                                   |
+----------------------------------------------------------------+

Start of files:
Size is 0 if it is a directory. Name index is the 0-index into the Name rows which contains the filename.
Piece number is ignored for a regular file, 0-indexed otherewise.
File version must be incremented if the hash changes since it is used to quickly check for changes without rehashing.
Permissions is not implemented so it's reserved.
Parent index is a 0 index into the files which specifies where the parent is. If the parent index is the same as the
  current index, it is at the root.
+----------------------------------------------------------------+
|32B hash info and file multipart8BFileSz8BNamePt4BPN4BFV4BPM4BPI|
+----------------------------------------------------------------+
(Size, Name Index, Piece Number, File Version, Permissions, Parent Index)

Name section:
+----------------------------------------------------------------+
|8 Byte Size, 4 Byte Number of Rows, 60 Bytes reserved           |
+----------------------------------------------------------------+
|The number of name rows specified. File names are tightly packed|
|and are null terminated.                                        |
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
Since I am changing to a more hosted version of this, the peers definition will likely change.
```

## Workers Protocol
This is not the final protocol but I wanted to write out some of my ideas. So the plan is to use [Cloudflare
Workers](https://workers.cloudflare.com/) and their [Key-Value Storage](https://www.cloudflare.com/products/workers-kv/)
as a way of avoiding many of the issues of true decentralization without significant cost. So what will happen is that
when you want to create a new waterfall file, you will send a request to the workers with the hash of your waterfall
file (the hash of the name and salt that is in the header), the datestamp that you made the update on, a ZT address to
find the full waterfall file at, your signature of the message, and your public key. If there is already a waterfall
with that hash, it will check that you are using the same public key and that the timestamp is new. If so, then it will
make the update. If there was no waterfall with that hash already, then it will just save it. I will use reasonable
timeouts and possibly figure out some other way to avoid people creating huge numbers of unused waterfalls. When someone
wants to get a waterfall, they will query the workers and ask for the hash. It will then be provided with the datestamp,
ZT address, and signature.

## Inter-node Protocol
I have this a lot less thought out. I know that there will need to be a ping command, a command to ask if someone has a
piece, and a command to ask for a piece. The idea is that when a node needs a piece, it will ask the best peers if they
have it. If they do, then it will ask for the piece. If none, then it will ask one of them for it anyway and it will try
to see if it can find it. If none can find it then the originating node will be asked. When the originating node makes
an update, besides uploading to the workers, it will also send it out to its best peers. When a node receives a new
version, it will send it out to its best best peers. The node will also directly ask the node who gave it a new version
for the new pieces. If that node was the originator then it will have those pieces. Otherwise, it will probably be
downloading them already anyway and can easily forward them. This is one of the best parts of the protocol. When an
update is made, there won't be an excess amount of traffic to the original node with everyone asking for it, most nodes
will get the new pieces forwarded to them by another node. There will also need to be a response so that a node can
indicate that it has never even heard of the file being asked for.

## Actual code or demos or anything interesting
TODO

# Build
All of this build info is deprecated since I am using Rust now. I am keeping this around to remember some of what I did
before.

I am currently building with clang so you will need that. I have a very basic makefile. You may need to remove `-Werror`
if you have a different version of clang than I do which has new warnings. That will eventually be changed by a debug vs
release flag but right now everything is debug so that's not a concern. I am working on macOS but I am also testing on
Linux so everything for now should work on both. I do plan to abstract things enough so that they can work well on
macOS, Windows, and Linux and have a slightly reduced features on other POSIX OSs. I don't plan to support big-endian
since that seems like a huge amount of extra effort and most big-endian platforms have a little-endian mode (according
to Wikipedia).

Anyway, as of now, you will need to install libsodium headers. On macOS with HomeBrew, that is as simple as `brew
install sodium` and on Ubuntu that is `apt install libsodium-dev`. You will also have to create a build directory since
I haven't added that to make yet. Then you just need to run `make` and that's about it. The output is in the build
directory.

# Usage
The following is what was working before I switched to Rust. I will change this once I get to the point where this is
all working in Rust.

As of writing this, I have only implemented a very basic creation of waterfall files from a file or directory. When you
run the program, you must provide the name of the output waterfall file and the input file or directory as arguments.
There is no way to easily check if it worked correctly. Right now I am just opening it in a hex editor with 64 byte rows
and checking against the spec. I want to tighten up this creation system a little more before reading and actually doing
stuff. It's getting there though!
