// SPDX-License-Identifier: GPL-2.0

//! Avalon devices hardware interface

//! use kernel::prelude::*;
use kernel::io::IoRaw;
use kernel::error::Result;

#[repr(packed)]
struct DmaControlRegs {
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

///struct DmaControl {
///    rc_src_lo: u32,     /// ::from_le_bytes(self.rc_src_lo),
///    rc_src_hi: u32,     /// ::from_le_bytes(self.rc_src_hi),
///    ep_dst_lo: u32,     /// ::from_le_bytes(self.ep_dst_lo),
///    ep_dst_hi: u32,     /// ::from_le_bytes(self.ep_dst_hi),
///    last_ptr: u32,      /// ::from_le_bytes(self.last_ptr),
///    table_size: u32,    /// ::from_le_bytes(self.table_size),
///    control: u32,       /// ::from_le_bytes(self.control),
///}

struct DmaControl {
    base: usize,
    regs: IoRaw::<{ core::mem::size_of::<DmaControlRegs>() }>,
}

impl DmaControl {
    fn new(ctrl_base: usize, ctrl_off: usize) -> Result<Self> {
        let base = ctrl_base + ctrl_off;
        let regs = IoRaw::<{ core::mem::size_of::<DmaControlRegs>() }>::new(base, core::mem::size_of::<DmaControlRegs>())?;

        Ok(Self { base, regs })
    }

    fn startXfer(self, rc_src_hi: u32, rc_src_lo: u32, ep_dst_hi: u32, ep_dst_lo: u32, last_id: i32) {
        let regs = IoRaw::<{ core::mem::size_of::<DmaControlRegs>() }>::new(self.base, core::mem::size_of::<DmaControlRegs>());

        regs.write32(rc_src_hi, core::mem::offset_of!(DmaControlRegs, rc_src_hi));
        regs.write32(rc_src_hi, core::mem::offset_of!(DmaControlRegs, rc_src_hi));
        regs.write32(rc_src_lo, core::mem::offset_of!(DmaControlRegs, rc_src_lo));
        regs.write32(ep_dst_hi, core::mem::offset_of!(DmaControlRegs, ep_dst_hi));
        regs.write32(ep_dst_lo, core::mem::offset_of!(DmaControlRegs, ep_dst_lo));
        regs.write32(last_id, core::mem::offset_of!(DmaControlRegs, table_size));
        regs.write32(last_id, core::mem::offset_of!(DmaControlRegs, last_ptr));
    }
}

#[allow(missing_docs)]
#[no_mangle]
pub extern "C" fn start_xfer(_base: usize, _ctrl_off: usize,
    _rc_src_hi: u32, _rc_src_lo: u32,
    _ep_dst_hi: u32, _ep_dst_lo: u32,
    _last_id: i32) -> i32 {
    0
}
