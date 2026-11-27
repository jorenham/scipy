//! Scalar C ABI over xsf-rust, for a SciPy feasibility demo.
//!
//! Every function writes an `sf_error_t` code to `err` (if not null) and returns NaN after a
//! caught panic.

use std::os::raw::c_int;
use std::panic::{AssertUnwindSafe, catch_unwind};

use num_complex::Complex64; // `#[repr(C)]`, the same layout as `std::complex<double>`

const SF_ERROR_OK: c_int = 0;
const SF_ERROR_OVERFLOW: c_int = 3;
const SF_ERROR_DOMAIN: c_int = 7;
const SF_ERROR_OTHER: c_int = 9;

/// xsf-rust returns only the value, so the error code is inferred from it: NaN from non-NaN
/// inputs is a domain error, an infinity from finite inputs an overflow.
fn guard(args: &[f64], err: Option<&mut c_int>, f: impl FnOnce() -> Complex64) -> Complex64 {
    let (r, code) = match catch_unwind(AssertUnwindSafe(f)) {
        Ok(r) if r.is_nan() && !args.iter().any(|x| x.is_nan()) => (r, SF_ERROR_DOMAIN),
        Ok(r) if r.is_infinite() && args.iter().all(|x| x.is_finite()) => (r, SF_ERROR_OVERFLOW),
        Ok(r) => (r, SF_ERROR_OK),
        Err(_) => (Complex64::new(f64::NAN, f64::NAN), SF_ERROR_OTHER),
    };
    if let Some(err) = err {
        *err = code;
    }
    r
}

#[unsafe(no_mangle)]
pub extern "C" fn xsf_hyp2f1(a: f64, b: f64, c: f64, x: f64, err: Option<&mut c_int>) -> f64 {
    guard(&[a, b, c, x], err, || xsf::hyp2f1(a, b, c, x).into()).re
}

#[unsafe(no_mangle)]
pub extern "C" fn xsf_hyp2f1_c(
    a: f64,
    b: f64,
    c: f64,
    z: Complex64,
    err: Option<&mut c_int>,
) -> Complex64 {
    guard(&[a, b, c, z.re, z.im], err, || xsf::hyp2f1(a, b, c, z))
}

#[unsafe(no_mangle)]
pub extern "C" fn xsf_hyp1f1_c(a: f64, b: f64, z: Complex64, err: Option<&mut c_int>) -> Complex64 {
    guard(&[a, b, z.re, z.im], err, || xsf::hyp1f1(a, b, z))
}

#[unsafe(no_mangle)]
pub extern "C" fn xsf_gammainc(a: f64, x: f64, err: Option<&mut c_int>) -> f64 {
    guard(&[a, x], err, || xsf::gammainc(a, x).into()).re
}

#[unsafe(no_mangle)]
pub extern "C" fn xsf_pdtr(k: f64, m: f64, err: Option<&mut c_int>) -> f64 {
    guard(&[k, m], err, || xsf::pdtr(k, m).into()).re
}

#[unsafe(no_mangle)]
pub extern "C" fn xsf_chdtr(df: f64, x: f64, err: Option<&mut c_int>) -> f64 {
    guard(&[df, x], err, || xsf::chdtr(df, x).into()).re
}
