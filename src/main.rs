#![feature(never_type)]
#![feature(try_trait_v2)]
#![feature(control_flow_enum)]

#![warn(deprecated_in_future)]
#![warn(future_incompatible)]
#![warn(nonstandard_style)]
#![warn(rust_2018_compatibility)]
#![warn(rust_2018_idioms)]
#![warn(trivial_casts, trivial_numeric_casts)]
#![warn(unused)]

#![warn(clippy::all, clippy::pedantic)]
#![allow(trivial_casts)]
// #![allow(clippy::cast_precision_loss)]
// #![allow(clippy::cast_possible_truncation)]
// #![allow(clippy::cast_possible_wrap)]
// #![allow(clippy::cast_sign_loss)]
// #![allow(clippy::enum_glob_use)]
// #![allow(clippy::map_unwrap_or)]
// #![allow(clippy::match_same_arms)]
// #![allow(clippy::module_name_repetitions)]
// #![allow(clippy::non_ascii_literal)]
// #![allow(clippy::option_if_let_else)]
#![allow(clippy::too_many_lines)]
// #![allow(clippy::unnested_or_patterns)] // TODO: remove this when we support Rust 1.53.0
// #![allow(clippy::unused_self)]
// #![allow(clippy::upper_case_acronyms)]
// #![allow(clippy::wildcard_imports)]
#![allow(clippy::similar_names)]

use std::{
    collections::HashMap,
    convert::{TryFrom, TryInto},
    env,
    error::Error,
    ffi::{CStr, CString},
    fmt::{
        Display,
        Formatter,
        self
    },
    fs::{self, File},
    io::{Read, Write},
    path::Path,
    process
};

use memmap2::Mmap;
use sodiumoxide::{
    self,
    crypto::{
        hash::sha256,
        sign::{
            self,
            SecretKey,
            Signature,
            State
        }
    },
    randombytes
};

const VERSION_MAJOR: u32 = 0;
const VERSION_MINOR: u32 = 2;
const VERSION_PATCH: u32 = 0;

const HEADER_SIZE: usize = 64 * 10;
const MAGIC_STRING: &str = "Waterfall data by Bryce Wilson!!";


fn main() {
    // NOTE(bryce): Process arguments
    let mut output_file_name: Option<String> = None;
    let mut source_file_name: Option<String> = None;
    let mut upload = false;
    for arg in env::args().skip(1) {
        if arg == "-y" {
            upload = true;
        } else if output_file_name.is_none() {
            output_file_name = Some(arg);
        } else if source_file_name.is_none() {
            source_file_name = Some(arg);
        } else {
            usage::<()>("Too many arguments. Expected: bytefall dest.waterfall src [-y]");
        }
    }
    let output_file_name = output_file_name.unwrap_or_else(|| usage("Not enough arguments."));
    let source_file_name = source_file_name.unwrap_or_else(|| usage("Not enough arguments."));
    let upload = upload; // For use later when we upload to the API
    // NOTE(bryce): We need this later for the tree processing but we need to do it early since we
    //  borrow source_fill_name.
    let mut path: String = source_file_name.clone();

    // NOTE(bryce): Create the new header including checking the old header
    let mut header = WaterfallHeader {
        magic_string: <[u8; 32]>::try_from(MAGIC_STRING.as_bytes()).unwrap(),
        version_major: VERSION_MAJOR,
        version_minor: VERSION_MINOR,
        version_patch: VERSION_PATCH,
        name: [0; 64*4],
        salt: [0; 64],
        waterfall_hash: [0; 32],
        pk: [0; 32],
        sk: [0; 64]
    };
    header.name[..source_file_name.len()].clone_from_slice(source_file_name.as_bytes());
    header.name[header.name.len() - 1] = 0;

    sodiumoxide::init().unwrap_or_else(|_| usage("Libsodium failed to initialize (unknown reason)."));

    // NOTE(bryce): This is fancy syntax to let me use the ? operator and otherwise leave a block
    //  early. The first part is basically a big validator that does some stuff if everything
    //  validates.
    let mut old_file_tree: Option<Vec<WaterfallFile>> = None;
    let (mut pk, mut sk) = sign::gen_keypair();
    match (|| -> Result<(), Box<dyn Error>> {
        let mut file = File::open(&output_file_name)?;
        let metadata = file.metadata()?;
        if !metadata.is_file() {return Err(GenericError("Existing Waterfall is not a file.").alloc())}
        if metadata.len() < HEADER_SIZE as u64 {return Err(GenericError("Existing Waterfall is corrupted (not large enough for the header).").alloc())}

        let mut old_header = [0_u8; HEADER_SIZE];
        file.read_exact(&mut old_header)?;
        let old_header = old_header;

        if &old_header[..MAGIC_STRING.len()] != MAGIC_STRING.as_bytes() {return Err(GenericError("Existing Waterfall is corrupted (magic string is wrong).").alloc())}

        let expected_hash = sha256::hash(&old_header[64..64*6]);
        if old_header[64*6..64*6+32] != expected_hash.0 {return Err(GenericError("Existing Waterfall is corrupted (Waterfall hash does not match expected hash).").alloc())}

        sk = SecretKey::from_slice(&old_header[64*9..64*10]).unwrap();
        pk = sk.public_key();
        if pk.0 != old_header[64*6+32..64*7] {return Err(GenericError("Existing Waterfall is corrupted or did not contain private key (private and public key do not match).").alloc())}

        let mut state = State::init();
        state.update(&old_header[..8*64]);
        if state.verify(&Signature::from_slice(&old_header[64*8..64*9]).unwrap(), &pk) {
            return Err(GenericError("Existing Waterfall is corrupted (header signature is not valid).").alloc());
        }

        /*
         * NOTE(bryce): Now we know that at least the header of the old file is val
         *  * It's a normal file
         *  * It has a size at least as long as is required for a header
         *  * The magic string is correct
         *  * The Waterfall hash is correct for the given name and salt
         *  * The secret key is present and the public key matches the one given
         *  * The header signature is valid
         *  At this point, we can put the existing values into our new header
         *  Get the existing salt, hash, pk, and sk
         */

        header.name.clone_from_slice(&old_header[64..64*5]);
        header.salt.clone_from_slice(&old_header[64*5..64*6]);
        header.waterfall_hash.clone_from_slice(&old_header[64*6..64*6+32]);
        header.pk.clone_from_slice(&old_header[64*6+32..64*7]);
        header.sk.clone_from_slice(&old_header[64*9..64*10]);

        let file = unsafe { Mmap::map(&file) }?;
        let file_count = u32::from_le_bytes(file[64*7..64*7+4].try_into().unwrap());
        let after_files = HEADER_SIZE + 64*file_count as usize;
        if file.len() < after_files + 64 {return Err(GenericError("Existing Waterfall is corrupted (file size not large enough for files and name header).").alloc())}
        let name_rows = u32::from_le_bytes(file[after_files + 8..after_files + 8 + 4].try_into().unwrap());
        let name_len = u64::from_le_bytes(file[after_files..after_files + 8].try_into().unwrap());
        if name_len > u64::from(name_rows) * 64 {return Err(GenericError("Existing Waterfall is corrupted (name length is longer than name rows can handle).").alloc())};
        if file.len() < after_files + 64 + 64*name_rows as usize + 64 {return Err(GenericError("Existing Waterfall is corrupted (file size not large enough for names and footer).").alloc())};
        let names = &file[after_files + 64..after_files + 64 + usize::try_from(name_len)?];
        let files = file[HEADER_SIZE..after_files].chunks_exact(64);
        if *names.last().unwrap() != 0 {return Err(GenericError("Existing Waterfall is corrupted (the names does not end in a 0 byte).").alloc())};

        let after_names = after_files + 64 + 64 * name_rows as usize;
        let mut state = State::init();
        state.update(&file[HEADER_SIZE..after_names]);
        if !state.verify(&Signature::from_slice(&file[after_names..after_names + 64]).unwrap(), &pk) {
            return Err(GenericError("Existing Waterfall is corrupted (the footer signature could not be verified).").alloc());
        }

        // NOTE(bryce): Parse the existing files
        old_file_tree = Some(vec![]);
        let mut parent_map: HashMap<u32, u32> = HashMap::new();
        for (index, file) in files.enumerate() {
            let name: *const u8 = &names[usize::try_from(u64::from_le_bytes(file[32+8..32+8*2].try_into().unwrap()))?];
            let mut parent = u32::from_le_bytes(file[32+8*2+4*3..32+8*2+4*4].try_into().unwrap());
            let old_parent = parent;
            if parent == index.try_into().unwrap() {
                parent = old_file_tree.as_ref().unwrap().len().try_into().unwrap()
            } else if let Some(new_parent) = parent_map.get(&parent) {
                parent = *new_parent;
            } else {
                continue;
            }
            old_file_tree.as_mut().unwrap().push(
                WaterfallFile {
                    hash: file[..32].try_into().unwrap(),
                    size: u64::from_le_bytes(file[32..32+8].try_into().unwrap()),
                    piece_number: u32::from_le_bytes(file[32+8*2..32+8*2+4].try_into().unwrap()),
                    version: u32::from_le_bytes(file[32+8*2+4..32+8*2+4*2].try_into().unwrap()),
                    name: unsafe { CStr::from_ptr(name.cast()) }.to_owned(),
                    parent
                }
            );
            parent_map.insert(old_parent, parent);
        }

        Ok(())
    })() {
        // NOTE(bryce): If everything did validate
        Ok(_) => {}
        // NOTE(bryce): Something didn't validate so we have to build everything ourselves.
        Err(e) => {
            // Create the salt, hash, pk, and sk
            old_file_tree = None;
            eprintln!("{}", e);
            randombytes::randombytes_into(&mut header.salt);
            let mut hash = sha256::State::new();
            hash.update(&header.name);
            hash.update(&header.salt);
            header.waterfall_hash.clone_from_slice(&hash.finalize().0);
            let (new_pk, new_sk) = sign::gen_keypair();
            pk = new_pk;
            sk = new_sk;
            header.pk.clone_from_slice(&pk.0);
            header.sk.clone_from_slice(&sk.0);
        },
    }

    // NOTE(bryce): Process the file tree
    let mut files: Vec<WaterfallFile> = vec![];
    path.push('/');
    process_tree(&mut files, 0, &mut path);

    // NOTE(bryce): Update the new tree based on the old one
    // TODO(bryce): Make this not be n^2 by adding the full path and some maps
    if let Some(old_files) = old_file_tree {
        for old_file in &old_files {
            let mut indexes: Vec<u32> = vec![];
            for file in &files {
                if file.name == old_file.name && file.hash != old_file.hash {
                    let mut old_parent = &old_files[usize::try_from(old_file.parent).unwrap()];
                    let mut parent = &files[usize::try_from(file.parent).unwrap()];
                    while old_parent.hash != old_file.hash {
                        if parent.name != old_parent.name {
                            continue;
                        }
                        old_parent = &old_files[usize::try_from(old_parent.parent).unwrap()];
                        parent = &files[usize::try_from(parent.parent).unwrap()];
                    }
                    if parent.hash == files[usize::try_from(parent.parent).unwrap()].hash {
                        indexes.push(parent.parent);
                    }
                }
            }
            for index in indexes {
                files[usize::try_from(index).unwrap()].version += 1;
            }
        }
    }

    match (|| -> Result<(), Box<dyn Error>> {
        // NOTE(bryce): Write this to a file!
        let mut file = File::create(output_file_name).unwrap_or_else(|_| {eprintln!("Could not open output file.");process::exit(1)});
        let mut header_sig = State::init();
        // TODO(bryce): Deal with the possible write errors. Maybe another closure trick is in order
        file.write_all(&header.magic_string)?;
        header_sig.update(&header.magic_string);
        file.write_all(&header.version_major.to_le_bytes())?;
        header_sig.update(&header.version_major.to_le_bytes());
        file.write_all(&header.version_minor.to_le_bytes())?;
        header_sig.update(&header.version_minor.to_le_bytes());
        file.write_all(&header.version_patch.to_le_bytes())?;
        header_sig.update(&header.version_patch.to_le_bytes());
        file.write_all(&[0; 20])?; // Padding
        header_sig.update(&[0; 20]);

        file.write_all(&header.name)?;
        header_sig.update(&header.name);
        file.write_all(&header.salt)?;
        header_sig.update(&header.salt);

        file.write_all(&header.waterfall_hash)?;
        header_sig.update(&header.waterfall_hash);
        file.write_all(&header.pk)?;
        header_sig.update(&header.pk);

        file.write_all(&u32::try_from(files.len()).unwrap().to_le_bytes())?;
        file.write_all(&[0; 60])?; // Padding

        // NOTE(bryce): Write the header signature and secret key
        file.write_all(&header_sig.finalize(&sk).0)?;
        file.write_all(&header.sk)?;

        // NOTE(bryce): Turn the files into something we can write
        let mut files_bytes: Vec<u8> = Vec::with_capacity(files.len()*64);
        let mut names_bytes: Vec<u8> = Vec::with_capacity(files.len()*10);
        for file in &files {
            files_bytes.extend_from_slice(&file.hash);
            files_bytes.extend_from_slice(&file.size.to_le_bytes());

            // NOTE(bryce): Deal with name
            files_bytes.extend_from_slice(&u64::try_from(names_bytes.len()).unwrap().to_le_bytes());
            names_bytes.extend_from_slice(file.name.to_bytes_with_nul());

            files_bytes.extend_from_slice(&file.piece_number.to_le_bytes());
            files_bytes.extend_from_slice(&file.version.to_le_bytes());
            files_bytes.extend_from_slice(&[0; 4]); // Permissions are currently reserved
            files_bytes.extend_from_slice(&file.parent.to_le_bytes());
        }
        file.write_all(&files_bytes)?;
        let mut footer_sig = State::init();
        footer_sig.update(&files_bytes);

        // NOTE(bryce): Deal with the name rows
        file.write_all(&u64::try_from(names_bytes.len()).unwrap().to_le_bytes())?;
        footer_sig.update(&u64::try_from(names_bytes.len()).unwrap().to_le_bytes());
        names_bytes.extend_from_slice(&vec![0; 64 - names_bytes.len() % 64]);
        let rows = match u32::try_from(names_bytes.len() / 64) {
            Ok(k)=>k,
            Err(e)=>{eprintln!("{}: There must have been way too many files. There were {} bytes and {} rows", e, names_bytes.len(), names_bytes.len() / 64);return Ok(())}
        };
        file.write_all(&rows.to_le_bytes())?;
        footer_sig.update(&rows.to_le_bytes());
        file.write_all(&[0; 52])?; // Padding
        footer_sig.update(&[0; 52]);
        file.write_all(&names_bytes)?;
        footer_sig.update(&names_bytes);

        // NOTE(bryce): Write the footer signature and padding
        file.write_all(&footer_sig.finalize(&sk).0)?;
        // NOTE(bryce): We are ignoring the peers for now.

        Ok(())
    })() {
        Ok(_) => {},
        Err(e) => {
            eprintln!("Error in writing the file: {:?}", e)
        }
    }

}

fn usage<T>(error: &str) -> T {
    eprintln!("{}", error);
    eprintln!("Usage: {} dest.waterfall src [-y]",
              env::args().next().unwrap_or_else(|| "bytefall".to_owned()));
    process::exit(1)
}

fn process_tree(files: &mut Vec<WaterfallFile>, parent: u32, path: &mut String) {
    // NOTE(bryce): Get the file info
    // TODO(bryce): Make a macro for these error conditions
    let file = match File::open(&path) {Ok(k)=>k,Err(e)=>{eprintln!("{}: {}",path,e);return}};
    let metadata = match file.metadata() {Ok(k)=>k,Err(e)=>{eprintln!("{}: {}",path,e);return}};
    // NOTE(bryce): We don't allow zero-length files, only directories.
    if metadata.len() == 0 && metadata.is_file() {return}

    // NOTE(bryce): Fill in basic info about the file
    files.push(WaterfallFile {
        hash: [0; 32],
        size: if metadata.is_file() {metadata.len()} else {0},
        piece_number: 0,
        version: 0,
        name: if let Some(k) = Path::new(&path).file_name() {
            if let Some(j) = k.to_str() {
                if let Ok(s) = CString::new(j) { s } else {
                    eprintln!("{}: File name could not be parsed. Maybe a zero-byte got in it somehow.\n{:?}", path, j.as_bytes());return
                }
            } else {
                eprintln!("{}: File name could not be parsed. Maybe it's not UTF-8?\n{:?}", path, k);return
            }
        } else {
            eprintln!("{}: File name could not be parsed",path);return
        },
        parent
    });
    let last_file = files.len() - 1;

    // NOTE(bryce): Hash the file
    let mut hash = sha256::State::new();
    hash.update(files[last_file].name.as_bytes());
    if metadata.is_file() {
        let file = match unsafe { Mmap::map(&file) } {Ok(k)=>k,Err(e)=>{eprintln!("{}: {}", path, e);return}};
        hash.update(&file);
    }
    files[last_file].hash = hash.finalize().0;

    // NOTE(bryce): If it's a directory, we need to recurse into it
    if metadata.is_dir() {
        // NOTE(bryce): prepare to create the paths for the new files
        path.push('/');
        let base = path.len();

        let dir = match fs::read_dir(&path) {Ok(k)=>k,Err(e)=>{eprintln!("{}: {}",path,e);return}};
        for file in dir {
            let file = match file {Ok(k)=>k,Err(e)=>{eprintln!("{}: {}",path,e);return}};
            let name = file.file_name();
            let name = if let Some(s) = name.to_str() {s} else {eprintln!("{}: A file or directory within had an invalid name", &path[..base]); continue};
            // NOTE(bryce): These file names are restricted
            if let "." | ".." | "/" = name {continue};
            // NOTE(bryce): Add the new filename onto the end, replacing anything from a previous iteration
            path.replace_range(base.., name);
            // NOTE(bryce): Recurse into the next entry
            let last_file = if let Ok(s) = u32::try_from(last_file) {s} else {eprintln!("{}: Too many files! Only the 32 bit maximum is allowed", &path[..base]); continue};
            process_tree(files, last_file, path);
        }
    }
}


struct WaterfallHeader {
    magic_string: [u8; 32],
    version_major: u32,
    version_minor: u32,
    version_patch: u32,
    // padding: [u8; 20],

    name: [u8; 64*4],
    salt: [u8; 64],

    waterfall_hash: [u8; 32],
    pk: [u8; 32],

    // file_count: u32,
    // padding: [u8; 60],

    // signature: [u8; 64],

    sk: [u8; 64]
}

#[derive(Debug)]
struct WaterfallFile {
    hash: [u8; 32],
    size: u64,
    name: CString,
    piece_number: u32,
    version: u32,
    parent: u32
}

#[derive(Debug)]
struct GenericError<'a>(&'a str);
impl Display for GenericError<'_> {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), fmt::Error> {
        write!(f, "{}", self.0)
    }
}
impl Error for GenericError<'_> {}

trait Alloc { fn alloc(self) -> Box<Self>; }
impl<T> Alloc for T { fn alloc(self) -> Box<Self> { Box::new(self) } }
