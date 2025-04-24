extern crate sse_client;
use sse_client::EventSource;
use std::fs::File;
use std::io::prelude::*;
use anyhow::Error;

fn main() -> Result<(), Error>{
    let mut i: u32 = 0;
    let mut filename: String = String::from("image");
    loop{
        filename.push_str(&i.to_string());
        let mut file = File::create(&filename)?;
        let event_source = EventSource::new("http://192.168.4.1/sse2").unwrap();
        let data: String = event_source.receiver().recv()?.data;
        file.write_all(&data.as_bytes()[1..])?;
        i += 1;
        filename = String::from("image");
    }
}
