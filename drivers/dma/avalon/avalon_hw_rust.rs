// SPDX-License-Identifier: GPL-2.0

//! Avalon devices hardware interface

//! use kernel::prelude::*;
use kernel::io::IoRaw;

#[repr(packed)]
struct DmaControl {
    rc_src_lo: u32,
    rc_src_hi: u32,
    ep_dst_lo: u32,
    ep_dst_hi: u32,
    last_ptr: u32,
    table_size: u32,
    control: u32,
}

/// struct DmaControlHW {
///     rc_src_lo: [u8; 4],
///     rc_src_hi: [u8; 4],
///     ep_dst_lo: [u8; 4],
///     ep_dst_hi: [u8; 4],
///     last_ptr: [u8; 4],
///     table_size: [u8; 4],
///         control: [u8; 4],
///     }

///#[repr(packed)]
///struct DmaControl {
///    rc_src_lo: u32,     /// ::from_le_bytes(self.rc_src_lo),
///    rc_src_hi: u32,     /// ::from_le_bytes(self.rc_src_hi),
///    ep_dst_lo: u32,     /// ::from_le_bytes(self.ep_dst_lo),
///    ep_dst_hi: u32,     /// ::from_le_bytes(self.ep_dst_hi),
///    last_ptr: u32,      /// ::from_le_bytes(self.last_ptr),
///    table_size: u32,    /// ::from_le_bytes(self.table_size),
///    control: u32,       /// ::from_le_bytes(self.control),
///}

#[allow(missing_docs)]
#[no_mangle]
pub extern "C" fn start_xfer(base: usize, ctrl_off: usize,
    rc_src_hi: u32, _rc_src_lo: u32,
    _ep_dst_hi: u32, _ep_dst_lo: u32,
    _last_id: i32) {
    let iomem = IoRaw::<{ core::mem::size_of::<DmaControl>() }>::new(base + ctrl_off, core::mem::size_of::<DmaControl>());

    iomem.write32(rc_src_hi, core::mem::offset_of!(DmaControl, rc_src_hi));
/// av_write32(rc_src_hi, base, ctrl_off, rc_src_hi);
/// av_write32(rc_src_lo, base, ctrl_off, rc_src_lo);
/// av_write32(ep_dst_hi, base, ctrl_off, ep_dst_hi);
/// av_write32(ep_dst_lo, base, ctrl_off, ep_dst_lo);
/// av_write32(last_id, base, ctrl_off, table_size);
/// av_write32(last_id, base, ctrl_off, last_ptr);
}
