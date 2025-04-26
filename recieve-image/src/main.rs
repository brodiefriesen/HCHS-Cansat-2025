extern crate sse_client;
use sse_client::EventSource;
use std::fs::File;
use std::io::prelude::*;
use std::io;
use std::sync::mpsc::RecvError;
use anyhow::Error;

fn main() -> Result<(), Error>{
    let mut i: u32 = 0;
    let mut filename: String = String::from("image");
    loop{
        filename.push_str(&i.to_string());
        filename.push_str(".jpg");
        let mut file = File::create(&filename)?;
        let event_source = EventSource::new("http://192.168.4.1/sse2").unwrap();
        let rec = event_source.receiver().recv_timeout(1000).recv();
        while rec != Err(RecvError) {
            file.write_all(&rec.clone()?.data.as_bytes()[1..])?;
        let _ = io::stdin();
        i += 1;
        filename = String::from("image");
    }
}
