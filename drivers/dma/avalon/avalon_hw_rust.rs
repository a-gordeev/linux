// SPDX-License-Identifier: GPL-2.0

//! Avalon devices hardware interface

#![allow(non_snake_case)]
#![allow(non_camel_case_types)]
#![allow(unused_variables)]
#![allow(unused_assignments)]
#![allow(non_upper_case_globals)]
#![allow(dead_code)]

use kernel::bindings::*;
use kernel::prelude::*;
use kernel::error::code;
use core::cmp;

const AVALON_DMA_DESC_NUM:          c_uint = 128;

const AVALON_DMA_FIXUP_SIZE:        c_uint = 0x100;
const AVALON_DMA_MAX_TANSFER_SIZE:  c_uint = 0x100000 - AVALON_DMA_FIXUP_SIZE;

const DMA_DESC_MAX:                 c_uint = AVALON_DMA_DESC_NUM;

/// Must match C struct dma_desc
#[repr(C)]
#[repr(packed)]
pub struct dma_desc {
    src_lo: __le32,
    src_hi: __le32,
    dst_lo: __le32,
    dst_hi: __le32,
    ctl_dma_len: __le32,
    reserved: [__le32; 3],
}

/// Must match C struct dma_segment
#[repr(C)]
pub struct dma_segment {
    dma_addr: dma_addr_t,
    dma_len: c_uint,
}

extern "C" {
    fn setup_desc(
        desc: *const dma_desc,
        desc_id: __u32,
        dest: __u64,
        src: __u64,
        size: __u32
    );
}

struct setup_descs_result {
    nr_descs: usize,
    seg_stop: usize,
    seg_set: usize,
}

fn setup_descs(
    descs: &[dma_desc],
    mut desc_id: c_uint,
    direction: dma_transfer_direction,
    dev_addr: dma_addr_t,
    host_addr: dma_addr_t,
    dma_len: usize,
) -> Result<(usize, usize)> {
    let mut nr_descs: usize = 0;
    let mut set: usize = 0;
    let mut len: usize = dma_len;

    if desc_id >= DMA_DESC_MAX as c_uint {
        return Err(code::EINVAL);
    }

    let (mut src, mut dest) = match direction {
        dma_transfer_direction_DMA_MEM_TO_DEV => (host_addr, dev_addr),
        dma_transfer_direction_DMA_DEV_TO_MEM => (dev_addr, host_addr),
        _ => return Err(code::EINVAL),
    };

    while len > 0 {
        let xfer_len: usize = cmp::min(len, AVALON_DMA_MAX_TANSFER_SIZE as usize);

        unsafe {
            setup_desc(&descs[nr_descs], desc_id, dest, src, xfer_len as c_uint);
        }

        set += xfer_len;

        nr_descs += 1;
        if nr_descs >= DMA_DESC_MAX as usize {
            break;
        }

        desc_id += 1;
        if desc_id >= DMA_DESC_MAX {
            break;
        }

        dest += xfer_len as dma_addr_t;
        src += xfer_len as dma_addr_t;

        len -= xfer_len;
    }

    Ok((nr_descs, set))
}

fn setup_descs_sg(
    descs: &[dma_desc],
    mut desc_id: c_uint,
    direction: dma_transfer_direction,
    mut dev_addr: dma_addr_t,
    seg: &[dma_segment],
    nr_segs: usize,
    seg_start: usize,
    mut seg_off: usize,
) -> Result<setup_descs_result> {
    let mut cur_desc: usize = 0;
    let mut nr_descs: usize = 0;
    let mut nr_descs_set = 0;
    let mut dma_len_set = !0;
    let mut i: usize = 0;

    if seg_start >= nr_segs {
        return Err(code::EINVAL);
    }
    if !matches!(
        direction,
        dma_transfer_direction_DMA_DEV_TO_MEM | dma_transfer_direction_DMA_MEM_TO_DEV
    ) {
        return Err(code::EINVAL);
    }

    /*
     * Skip all SGEs that have been fully transmitted.
     */
    while i < seg_start {
        dev_addr += seg[i].dma_len as dma_addr_t;
        i += 1;
    }

    /*
     * Skip the current SGE if it has been fully transmitted.
     */
    if seg[i].dma_len as usize == seg_off {
        dev_addr += seg_off as dma_addr_t;
        seg_off = 0;
        i += 1;
    }

    /*
     * Setup as many SGEs as the controller is able to transmit.
     */
    while i < nr_segs {
        let mut dma_addr: dma_addr_t = seg[i].dma_addr;
        let mut dma_len: usize = seg[i].dma_len as usize;

        /*
         * The offset can not be longer than the SGE length.
         */
        if dma_len < seg_off {
            return Err(code::EINVAL);
        }

        if seg_off != 0 {
            dev_addr += seg_off as dma_addr_t;
            dma_addr += seg_off as dma_addr_t;
            dma_len -= seg_off;

            seg_off = 0;
        }

        let res = setup_descs(
            &descs[cur_desc..],
            desc_id,
            direction,
            dev_addr,
            dma_addr,
            dma_len,
        )?;
        (nr_descs_set, dma_len_set) = res;

        if desc_id as usize + nr_descs_set > DMA_DESC_MAX as usize || nr_descs + nr_descs_set > DMA_DESC_MAX as usize {
            return Err(code::EINVAL);
        }

        nr_descs += nr_descs_set;
        desc_id += nr_descs_set as c_uint;

        /*
         * Stop when descriptor table entries are exhausted.
         */
        if desc_id >= DMA_DESC_MAX {
            break;
        }

        /*
         * The descriptor table still has free entries, thus
         * the current SGE should have fit.
         */
        if dma_len as usize != dma_len_set {
            return Err(code::EINVAL);
        }

        if i >= nr_segs - 1 {
            break;
        }

        cur_desc += nr_descs_set;
        dev_addr += dma_len as dma_addr_t;

        i += 1;
    }

    /*
     * Remember the SGE that next transmission should be started from.
     */
    if nr_descs != 0 {
        Ok(setup_descs_result {
            nr_descs: nr_descs,
            seg_stop: i,
            seg_set: dma_len_set as usize,
        })
    } else {
        Ok(setup_descs_result {
            nr_descs: nr_descs,
            seg_stop: seg_start,
            seg_set: seg_off,
        })
    }
}

/// setup_descs_sg() rust replacement
#[no_mangle]
pub extern "C" fn setup_descs_sg_rust(
    descs: *const dma_desc,
    desc_id: c_uint,
    direction: dma_transfer_direction,
    dev_addr: dma_addr_t,
    seg: *const dma_segment,
    nr_segs: c_uint,
    seg_start: c_uint,
    seg_off: c_uint,
    seg_stop: *mut c_uint,
    seg_set: *mut c_uint,
) -> c_int {
    let descs_slice: &[dma_desc] = unsafe {
        core::slice::from_raw_parts(descs, AVALON_DMA_DESC_NUM as usize * core::mem::size_of::<dma_desc>())
    };
    let seg_slice: &[dma_segment] = unsafe {
        core::slice::from_raw_parts(seg, nr_segs as usize * core::mem::size_of::<dma_segment>())
    };
    let res = setup_descs_sg(
        descs_slice,
        desc_id,
        direction,
        dev_addr,
        seg_slice,
        nr_segs as usize,
        seg_start as usize,
        seg_off as usize
    );

    let s = match res {
        Err(e) => {
            return e.to_errno();
        },
        Ok(s) => {
            unsafe {
                *seg_stop = s.seg_stop as c_uint;
                *seg_set = s.seg_set as c_uint;
            }
            s
        },
    };

    s.nr_descs as c_int
}
