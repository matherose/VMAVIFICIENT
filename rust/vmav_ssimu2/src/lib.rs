//! C FFI wrapper around the `ssimulacra2` Rust crate.
//!
//! Exposes a single function that takes two YUV420P10 frames (as raw planes
//! coming straight from libav, with their byte strides) plus libav-style
//! color metadata, and returns the SSIMULACRA2 score.
//!
//! The integer values for `color_primaries`, `transfer_characteristics`, and
//! `matrix_coefficients` follow the ITU-T H.273 numbering, which is identical
//! to libav's `AVColorPrimaries` / `AVColorTransferCharacteristic` /
//! `AVColorSpace` enums — so the C caller can pass `frame->color_primaries`
//! (etc.) directly with no remapping.

use std::slice;

use num_traits::FromPrimitive;
use ssimulacra2::compute_frame_ssimulacra2;
use v_frame::{frame::Frame, pixel::ChromaSampling};
use yuvxyb::{ColorPrimaries, MatrixCoefficients, TransferCharacteristic, Yuv, YuvConfig};

/// Sentinel returned on any failure (invalid args, conversion error, etc.).
/// Real SSIMULACRA2 scores are in (-inf, 100]; -1.0 cannot be confused with one.
pub const VMAV_SSIMU2_ERROR: f64 = -1.0;

/// Score one pair of YUV420P10 frames against each other.
///
/// All pointers are borrowed for the duration of the call. Buffers must be
/// valid for at least `y_stride_bytes * height` (luma) and
/// `uv_stride_bytes * (height / 2)` (chroma) bytes. Pixel data is interpreted
/// as little-endian u16 values in the low 10 bits.
///
/// # Safety
/// Caller must ensure all six pointers reference valid memory of the sizes
/// described above.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vmav_ssimu2_score_yuv420p10(
    ref_y: *const u8,
    ref_u: *const u8,
    ref_v: *const u8,
    dis_y: *const u8,
    dis_u: *const u8,
    dis_v: *const u8,
    width: u32,
    height: u32,
    y_stride_bytes: u32,
    uv_stride_bytes: u32,
    color_primaries: u32,
    transfer_characteristics: u32,
    matrix_coefficients: u32,
    full_range: bool,
) -> f64 {
    // 4:2:0 needs even dimensions.
    if width < 2 || height < 2 || width % 2 != 0 || height % 2 != 0 {
        return VMAV_SSIMU2_ERROR;
    }
    if y_stride_bytes == 0 || uv_stride_bytes == 0 {
        return VMAV_SSIMU2_ERROR;
    }

    // Map libav enum ints (= ITU-T H.273 numbering) to yuvxyb enum variants.
    // av_data's enums implement FromPrimitive, so this just works.
    let Some(cp) = ColorPrimaries::from_u32(color_primaries) else {
        return VMAV_SSIMU2_ERROR;
    };
    let Some(tc) = TransferCharacteristic::from_u32(transfer_characteristics) else {
        return VMAV_SSIMU2_ERROR;
    };
    let Some(mc) = MatrixCoefficients::from_u32(matrix_coefficients) else {
        return VMAV_SSIMU2_ERROR;
    };

    let cfg = YuvConfig {
        bit_depth: 10,
        subsampling_x: 1,
        subsampling_y: 1,
        full_range,
        matrix_coefficients: mc,
        transfer_characteristics: tc,
        color_primaries: cp,
    };

    let yuv_ref = match build_yuv(
        ref_y,
        ref_u,
        ref_v,
        width as usize,
        height as usize,
        y_stride_bytes as usize,
        uv_stride_bytes as usize,
        cfg,
    ) {
        Some(y) => y,
        None => return VMAV_SSIMU2_ERROR,
    };
    let yuv_dis = match build_yuv(
        dis_y,
        dis_u,
        dis_v,
        width as usize,
        height as usize,
        y_stride_bytes as usize,
        uv_stride_bytes as usize,
        cfg,
    ) {
        Some(y) => y,
        None => return VMAV_SSIMU2_ERROR,
    };

    match compute_frame_ssimulacra2(yuv_ref, yuv_dis) {
        Ok(score) => score,
        Err(_) => VMAV_SSIMU2_ERROR,
    }
}

/// Copy three libav-style planes into a freshly allocated `Frame<u16>`,
/// then wrap it in a `Yuv<u16>` with the provided config.
#[allow(clippy::too_many_arguments)]
unsafe fn build_yuv(
    y_ptr: *const u8,
    u_ptr: *const u8,
    v_ptr: *const u8,
    width: usize,
    height: usize,
    y_stride_bytes: usize,
    uv_stride_bytes: usize,
    cfg: YuvConfig,
) -> Option<Yuv<u16>> {
    if y_ptr.is_null() || u_ptr.is_null() || v_ptr.is_null() {
        return None;
    }

    // Allocate a 4:2:0 frame. No padding — we only need visible pixels.
    let mut frame: Frame<u16> = Frame::new_with_padding(width, height, ChromaSampling::Cs420, 0);

    let chroma_height = height / 2;

    // Luma
    let y_bytes = y_stride_bytes.checked_mul(height)?;
    let y_slice = unsafe { slice::from_raw_parts(y_ptr, y_bytes) };
    frame.planes[0].copy_from_raw_u8(y_slice, y_stride_bytes, 2);

    // Chroma U
    let uv_bytes = uv_stride_bytes.checked_mul(chroma_height)?;
    let u_slice = unsafe { slice::from_raw_parts(u_ptr, uv_bytes) };
    frame.planes[1].copy_from_raw_u8(u_slice, uv_stride_bytes, 2);

    // Chroma V
    let v_slice = unsafe { slice::from_raw_parts(v_ptr, uv_bytes) };
    frame.planes[2].copy_from_raw_u8(v_slice, uv_stride_bytes, 2);

    Yuv::<u16>::new(frame, cfg).ok()
}
