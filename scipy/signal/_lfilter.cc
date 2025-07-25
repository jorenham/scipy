/*
 * vim:syntax=c
 * vim:sw=4
 */
#include <Python.h>
#define PY_ARRAY_UNIQUE_SYMBOL _scipy_signal_ARRAY_API
#define NO_IMPORT_ARRAY
#include <numpy/ndarrayobject.h>
#include "npy_2_compat.h"

#include "_sigtools.hh"


template<int TYPECODE>
static void filt(char *b, char *a, char *x, char *y, char *Z,
                 npy_intp len_b, npy_uintp len_x, npy_intp stride_X,
                 npy_intp stride_Y);

template<int TYPECODE>
static void cmplx_filt(char *b, char *a, char *x, char *y, char *Z,
                       npy_intp len_b, npy_uintp len_x, npy_intp stride_X,
                       npy_intp stride_Y);

typedef void (BasicFilterFunction) (char *, char *,  char *, char *, char *,
                                    npy_intp, npy_uintp, npy_intp, npy_intp);

static BasicFilterFunction *BasicFilterFunctions[256];

void
scipy_signal__sigtools_linear_filter_module_init(void)
{
    int k;
    for (k = 0; k < 256; ++k) {
        BasicFilterFunctions[k] = NULL;
    }
    BasicFilterFunctions[NPY_FLOAT] = filt<NPY_FLOAT>;
    BasicFilterFunctions[NPY_DOUBLE] = filt<NPY_DOUBLE>;
    BasicFilterFunctions[NPY_LONGDOUBLE] = filt<NPY_LONGDOUBLE>;
    BasicFilterFunctions[NPY_CFLOAT] = cmplx_filt<NPY_CFLOAT>;
    BasicFilterFunctions[NPY_CDOUBLE] = cmplx_filt<NPY_CDOUBLE>;
    BasicFilterFunctions[NPY_CLONGDOUBLE] = cmplx_filt<NPY_CLONGDOUBLE>;
}

static int
RawFilter(const PyArrayObject * b, const PyArrayObject * a,
          const PyArrayObject * x, const PyArrayObject * zi,
          const PyArrayObject * zf, PyArrayObject * y, int axis,
          BasicFilterFunction * filter_func);

PyObject*
convert_shape_to_errmsg(npy_intp ndim, npy_intp *Xshape, npy_intp *Vishape,
                        npy_intp theaxis, npy_intp val)
{
    npy_intp j, expect_size;
    PyObject *msg, *tmp, *msg1, *tmp1;

    if (ndim == 1) {
        msg = PyUnicode_FromFormat("Unexpected shape for zi: expected (%"   \
                                    NPY_INTP_FMT ",), found (%" NPY_INTP_FMT \
                                    ",).", val, Vishape[0]);
        return msg;
    }

    msg = PyUnicode_FromString("Unexpected shape for zi:  expected (");
    if (!msg) {
        return 0;
    }

    msg1 = PyUnicode_FromString("), found (");
    if (!msg1) {
        Py_DECREF(msg);
        return 0;
    }

    for (j = 0; j < ndim; ++j) {
        expect_size = j != theaxis ? Xshape[j] : val;

        if (j == ndim - 1) {
            tmp = PyUnicode_FromFormat("%" NPY_INTP_FMT, expect_size);
            tmp1 = PyUnicode_FromFormat("%" NPY_INTP_FMT, Vishape[j]);
        } else {
            tmp = PyUnicode_FromFormat("%" NPY_INTP_FMT ",", expect_size);
            tmp1 = PyUnicode_FromFormat("%" NPY_INTP_FMT ",", Vishape[j]);
        }
        if (!tmp) {
            Py_DECREF(msg);
            Py_DECREF(msg1);
            Py_XDECREF(tmp1);
            return 0;
        }
        if (!tmp1) {
            Py_DECREF(msg);
            Py_DECREF(msg1);
            Py_DECREF(tmp);
            return 0;
        }
        Py_SETREF(msg, PyUnicode_Concat(msg, tmp));
        Py_SETREF(msg1, PyUnicode_Concat(msg1, tmp1));
        Py_DECREF(tmp);
        Py_DECREF(tmp1);
    }
    tmp = PyUnicode_FromString(").");
    if (!tmp) {
        Py_DECREF(msg);
        Py_DECREF(msg1);
        return 0;
    }
    Py_SETREF(msg1, PyUnicode_Concat(msg1, tmp));
    Py_SETREF(msg, PyUnicode_Concat(msg, msg1));
    Py_DECREF(tmp);
    Py_DECREF(msg1);
    return msg;
}

/*
 * XXX: Error checking not done yet
 */
PyObject*
scipy_signal__sigtools_linear_filter(PyObject * NPY_UNUSED(dummy), PyObject * args)
{
    PyObject *b, *a, *X, *Vi;
    PyArrayObject *arY, *arb, *ara, *arX, *arVi, *arVf;
    int axis, typenum, theaxis, st, Vi_needs_broadcasted = 0;
    char *ara_ptr, input_flag = 0, *azero;
    npy_intp na, nb, nal, zi_size;
    npy_intp zf_shape[NPY_MAXDIMS];
    BasicFilterFunction *basic_filter;

    axis = -1;
    Vi = NULL;
    if (!PyArg_ParseTuple(args, "OOO|iO", &b, &a, &X, &axis, &Vi)) {
        return NULL;
    }

    typenum = PyArray_ObjectType(b, 0);
    typenum = PyArray_ObjectType(a, typenum);
    typenum = PyArray_ObjectType(X, typenum);
    if (Vi != NULL) {
        typenum = PyArray_ObjectType(Vi, typenum);
    }

    arY = arVf = arVi = NULL;
    ara = (PyArrayObject *) PyArray_ContiguousFromObject(a, typenum, 1, 1);
    arb = (PyArrayObject *) PyArray_ContiguousFromObject(b, typenum, 1, 1);
    arX = (PyArrayObject *) PyArray_FromObject(X, typenum, 0, 0);
    /* XXX: fix failure handling here */
    if (ara == NULL || arb == NULL || arX == NULL) {
        PyErr_SetString(PyExc_ValueError,
                        "could not convert b, a, and x to a common type");
        goto fail;
    }

    if (axis < -PyArray_NDIM(arX) || axis > PyArray_NDIM(arX) - 1) {
        PyErr_SetString(PyExc_ValueError, "selected axis is out of range");
        goto fail;
    }
    if (axis < 0) {
        theaxis = PyArray_NDIM(arX) + axis;
    } else {
        theaxis = axis;
    }

    if (Vi != NULL) {
        arVi = (PyArrayObject *) PyArray_FromObject(Vi, typenum,
                                                    PyArray_NDIM(arX),
                                                    PyArray_NDIM(arX));
        if (arVi == NULL)
            goto fail;

        input_flag = 1;
    }

    if (PyArray_TYPE(arX) < 256) {
        basic_filter = BasicFilterFunctions[(int) (PyArray_TYPE(arX))];
    }
    else {
        basic_filter = NULL;
    }
    if (basic_filter == NULL) {
        PyObject *msg, *str;

        str = PyObject_Str((PyObject*)PyArray_DESCR(arX));
        if (str == NULL) {
            goto fail;
        }
        msg = PyUnicode_FromFormat(
                        "input type '%U' not supported\n", str);
        Py_DECREF(str);
        if (msg == NULL) {
            goto fail;
        }
        PyErr_SetObject(PyExc_NotImplementedError, msg);
        Py_DECREF(msg);
        goto fail;
    }

    /* Skip over leading zeros in vector representing denominator (a) */
    azero = PyArray_Zero(ara);
    if (azero == NULL) {
        goto fail;
    }
    ara_ptr = (char *)PyArray_DATA(ara);
    nal = PyArray_ITEMSIZE(ara);
    st = memcmp(ara_ptr, azero, nal);
    PyDataMem_FREE(azero);
    if (st == 0) {
        PyErr_SetString(PyExc_ValueError,
                        "BUG: filter coefficient a[0] == 0 not supported yet");
        goto fail;
    }

    na = PyArray_SIZE(ara);
    nb = PyArray_SIZE(arb);
    zi_size = (na > nb ? na : nb) - 1;
    if (input_flag) {
        npy_intp k, Vik, Xk;
        for (k = 0; k < PyArray_NDIM(arX); ++k) {
            Vik = PyArray_DIM(arVi, k);
            Xk = PyArray_DIM(arX, k);
            if (k == theaxis && Vik == zi_size) {
                zf_shape[k] = zi_size;
            } else if (k != theaxis && Vik == Xk) {
                zf_shape[k] = Xk;
            } else if (k != theaxis && Vik == 1) {
                zf_shape[k] = Xk;
                Vi_needs_broadcasted = 1;
            } else {
                PyObject *msg = convert_shape_to_errmsg(PyArray_NDIM(arX),
                        PyArray_DIMS(arX), PyArray_DIMS(arVi),
                        theaxis, zi_size);
                if (!msg) {
                    goto fail;
                }
                PyErr_SetObject(PyExc_ValueError, msg);
                Py_DECREF(msg);
                goto fail;
            }
        }

        if (Vi_needs_broadcasted) {
            /* Expand the singleton dimensions of arVi without copying by
             * creating a new view of arVi with the expanded dimensions
             * but the corresponding stride = 0.
             */
            PyArrayObject *arVi_view;
            PyArray_Descr *view_dtype;
            npy_intp *arVi_shape = PyArray_DIMS(arVi);
            npy_intp *arVi_strides = PyArray_STRIDES(arVi);
            npy_intp ndim = PyArray_NDIM(arVi);
            npy_intp strides[NPY_MAXDIMS];
            npy_intp k;

            for (k = 0; k < ndim; ++k) {
                if (arVi_shape[k] == 1) {
                    strides[k] = 0;
                } else {
                    strides[k] = arVi_strides[k];
                }
            }

            /* PyArray_DESCR borrows a reference, but it will be stolen
             * by PyArray_NewFromDescr below, so increment it.
             */
            view_dtype = PyArray_DESCR(arVi);
            Py_INCREF(view_dtype);

            arVi_view = (PyArrayObject *)PyArray_NewFromDescr(&PyArray_Type,
                            view_dtype, ndim, zf_shape, strides,
                            PyArray_BYTES(arVi), 0, NULL);
            if (!arVi_view) {
                goto fail;
            }
            /* Give our reference to arVi to arVi_view */
            if (PyArray_SetBaseObject(arVi_view, (PyObject *)arVi) == -1) {
                Py_DECREF(arVi_view);
                goto fail;
            }
            arVi = arVi_view;
        }

        arVf = (PyArrayObject *) PyArray_SimpleNew(PyArray_NDIM(arVi),
                                                   zf_shape,
                                                   typenum);
        if (arVf == NULL) {
            goto fail;
        }
    }

    arY = (PyArrayObject *) PyArray_SimpleNew(PyArray_NDIM(arX),
                                              PyArray_DIMS(arX), typenum);
    if (arY == NULL) {
        goto fail;
    }


    st = RawFilter(arb, ara, arX, arVi, arVf, arY, theaxis, basic_filter);
    if (st) {
        goto fail;
    }

    Py_XDECREF(ara);
    Py_XDECREF(arb);
    Py_XDECREF(arX);
    Py_XDECREF(arVi);

    if (!input_flag) {
        return PyArray_Return(arY);
    } else {
        return Py_BuildValue("(NN)", arY, arVf);
    }


  fail:
    Py_XDECREF(ara);
    Py_XDECREF(arb);
    Py_XDECREF(arX);
    Py_XDECREF(arVi);
    Py_XDECREF(arVf);
    Py_XDECREF(arY);
    return NULL;
}

/*
 * Copy the first nx items of x into xzfilled , and fill the rest with 0s
 */
static int
zfill(const PyArrayObject * x, npy_intp nx, char *xzfilled, npy_intp nxzfilled)
{
    char *xzero;
    npy_intp i, nxl;
    PyArray_CopySwapFunc *copyswap = PyDataType_GetArrFuncs(PyArray_DESCR((PyArrayObject *)x))->copyswap;

    nxl = PyArray_ITEMSIZE(x);

    /* PyArray_Zero does not take const pointer, hence the cast */
    xzero = PyArray_Zero((PyArrayObject *) x);
    if (xzero == NULL) return -1;

    if (nx > 0) {
        for (i = 0; i < nx; ++i) {
            copyswap(xzfilled + i * nxl,
                     (char *)PyArray_DATA((PyArrayObject *)x) + i * nxl,
                     0, NULL);
        }
    }
    for (i = nx; i < nxzfilled; ++i) {
        copyswap(xzfilled + i * nxl, xzero, 0, NULL);
    }

    PyDataMem_FREE(xzero);

    return 0;
}

/*
 * a and b assumed to be contiguous
 *
 * XXX: this code is very conservative, and could be considerably sped up for
 * the usual cases (like contiguity).
 *
 * XXX: the code should be refactored (at least with/without initial
 * condition), some code is wasteful here
 */
static int
RawFilter(const PyArrayObject * b, const PyArrayObject * a,
          const PyArrayObject * x, const PyArrayObject * zi,
          const PyArrayObject * zf, PyArrayObject * y, int axis,
          BasicFilterFunction * filter_func)
{
    PyArrayIterObject *itx, *ity, *itzi = NULL, *itzf = NULL;
    npy_intp nitx, i, nxl, nzfl, j;
    npy_intp na, nb, nal, nbl;
    npy_intp nfilt;
    char *azfilled, *bzfilled, *zfzfilled, *yoyo;
    PyArray_CopySwapFunc *copyswap = PyDataType_GetArrFuncs(PyArray_DESCR((PyArrayObject *)x))->copyswap;

    itx = (PyArrayIterObject *) PyArray_IterAllButAxis((PyObject *) x,
                                                       &axis);
    if (itx == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Could not create itx");
        goto fail;
    }
    nitx = itx->size;

    ity = (PyArrayIterObject *) PyArray_IterAllButAxis((PyObject *) y,
                                                       &axis);
    if (ity == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Could not create ity");
        goto clean_itx;
    }

    if (zi != NULL) {
        itzi = (PyArrayIterObject *) PyArray_IterAllButAxis((PyObject *)
                                                            zi, &axis);
        if (itzi == NULL) {
            PyErr_SetString(PyExc_MemoryError, "Could not create itzi");
            goto clean_ity;
        }

        itzf = (PyArrayIterObject *) PyArray_IterAllButAxis((PyObject *)
                                                            zf, &axis);
        if (itzf == NULL) {
            PyErr_SetString(PyExc_MemoryError, "Could not create itzf");
            goto clean_itzi;
        }
    }

    na = PyArray_SIZE((PyArrayObject *)a);
    nal = PyArray_ITEMSIZE(a);
    nb = PyArray_SIZE((PyArrayObject *)b);
    nbl = PyArray_ITEMSIZE(b);

    nfilt = na > nb ? na : nb;

    azfilled = (char *)malloc(nal * nfilt);
    if (azfilled == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Could not create azfilled");
        goto clean_itzf;
    }
    bzfilled = (char *)malloc(nbl * nfilt);
    if (bzfilled == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Could not create bzfilled");
        goto clean_azfilled;
    }

    nxl = PyArray_ITEMSIZE(x);
    zfzfilled = (char *)malloc(nxl * (nfilt - 1));
    if (zfzfilled == NULL) {
        PyErr_SetString(PyExc_MemoryError, "Could not create zfzfilled");
        goto clean_bzfilled;
    }
    /* Initialize zero filled buffers to 0, so that we can use
     * Py_XINCREF/Py_XDECREF on it for object arrays (necessary for
     * copyswap to work correctly). Strictly speaking, it is not needed for
     * fundamental types (as values are copied instead of pointers, without
     * refcounts), but oh well...
     */
    memset(azfilled, 0, nal * nfilt);
    memset(bzfilled, 0, nbl * nfilt);
    memset(zfzfilled, 0, nxl * (nfilt - 1));

    if (zfill(a, na, azfilled, nfilt) == -1) goto clean_zfzfilled;
    if (zfill(b, nb, bzfilled, nfilt) == -1) goto clean_zfzfilled;

    /* XXX: Check that zf and zi have same type ? */
    if (zf != NULL) {
        nzfl = PyArray_ITEMSIZE(zf);
    } else {
        nzfl = 0;
    }

    /* Iterate over the input array */
    for (i = 0; i < nitx; ++i) {
        if (zi != NULL) {
            yoyo = itzi->dataptr;
            /* Copy initial conditions zi in zfzfilled buffer */
            for (j = 0; j < nfilt - 1; ++j) {
                copyswap(zfzfilled + j * nzfl, yoyo, 0, NULL);
                yoyo += itzi->strides[axis];
            }
            PyArray_ITER_NEXT(itzi);
        } else {
            if (zfill(x, 0, zfzfilled, nfilt - 1) == -1) goto clean_zfzfilled;
        }

        filter_func(bzfilled, azfilled,
                    itx->dataptr, ity->dataptr, zfzfilled,
                    nfilt, PyArray_DIM(x, axis), itx->strides[axis],
                    ity->strides[axis]);

        if (PyErr_Occurred()) {
            goto clean_zfzfilled;
        }
        PyArray_ITER_NEXT(itx);
        PyArray_ITER_NEXT(ity);

        /* Copy tmp buffer fo final values back into zf output array */
        if (zi != NULL) {
            yoyo = itzf->dataptr;
            for (j = 0; j < nfilt - 1; ++j) {
                copyswap(yoyo, zfzfilled + j * nzfl, 0, NULL);
                yoyo += itzf->strides[axis];
            }
            PyArray_ITER_NEXT(itzf);
        }
    }

    /* Free up allocated memory */
    free(zfzfilled);
    free(bzfilled);
    free(azfilled);

    if (zi != NULL) {
        Py_DECREF(itzf);
        Py_DECREF(itzi);
    }
    Py_DECREF(ity);
    Py_DECREF(itx);

    return 0;

clean_zfzfilled:
    free(zfzfilled);
clean_bzfilled:
    free(bzfilled);
clean_azfilled:
    free(azfilled);
clean_itzf:
    if (zf != NULL) {
        Py_DECREF(itzf);
    }
clean_itzi:
    if (zi != NULL) {
        Py_DECREF(itzi);
    }
clean_ity:
    Py_DECREF(ity);
clean_itx:
    Py_DECREF(itx);
fail:
    return -1;
}

/*****************************************************************
 *   This is code for a 1-D linear-filter along an arbitrary     *
 *   dimension of an N-D array.                                  *
 *****************************************************************/

template<int TYPECODE>
static void filt(char *b, char *a, char *x, char *y, char *Z,
                       npy_intp len_b, npy_uintp len_x, npy_intp stride_X,
                       npy_intp stride_Y)
{
    typedef typename traits<TYPECODE>::real_type real_type;

    Py_BEGIN_ALLOW_THREADS
    char *ptr_x = x, *ptr_y = y;
    real_type *ptr_Z;
    real_type *ptr_b = (real_type *)b;
    real_type *ptr_a = (real_type *)a;
    real_type *xn, *yn;
    const real_type a0 = *((real_type *) a);
    npy_intp n;
    npy_uintp k;

    /* normalize the filter coefs only once. */
    for (n = 0; n < len_b; ++n) {
        ptr_b[n] /= a0;
        ptr_a[n] /= a0;
    }

    for (k = 0; k < len_x; k++) {
        ptr_b = (real_type *) b;   /* Reset a and b pointers */
        ptr_a = (real_type *) a;
        xn = (real_type *) ptr_x;
        yn = (real_type *) ptr_y;
        if (len_b > 1) {
            ptr_Z = ((real_type *) Z);
            *yn = *ptr_Z + *ptr_b  * *xn;   /* Calculate first delay (output) */
            ptr_b++;
            ptr_a++;
            /* Fill in middle delays */
            for (n = 0; n < len_b - 2; n++) {
                *ptr_Z =
                    ptr_Z[1] + *xn * (*ptr_b) - *yn * (*ptr_a);
                ptr_b++;
                ptr_a++;
                ptr_Z++;
            }
            /* Calculate last delay */
            *ptr_Z = *xn * (*ptr_b) - *yn * (*ptr_a);
        } else {
            *yn = *xn * (*ptr_b);
        }

        ptr_y += stride_Y;      /* Move to next input/output point */
        ptr_x += stride_X;
    }
    Py_END_ALLOW_THREADS
}


template<int TYPECODE>
static void cmplx_filt(char *b, char *a, char *x, char *y, char *Z,
                        npy_intp len_b, npy_uintp len_x, npy_intp stride_X,
                        npy_intp stride_Y)
{
    typedef typename traits<TYPECODE>::real_type real_type;

    Py_BEGIN_ALLOW_THREADS
    char *ptr_x = x, *ptr_y = y;
    real_type *ptr_Z, *ptr_b;
    real_type *ptr_a;
    real_type *xn, *yn;
    real_type a0r = ((real_type *) a)[0];
    real_type a0i = ((real_type *) a)[1];
    real_type a0_mag, tmpr, tmpi;
    npy_intp n;
    npy_uintp k;

    a0_mag = a0r * a0r + a0i * a0i;
    for (k = 0; k < len_x; k++) {
        ptr_b = (real_type *) b;   /* Reset a and b pointers */
        ptr_a = (real_type *) a;
        xn = (real_type *) ptr_x;
        yn = (real_type *) ptr_y;
        if (len_b > 1) {
            ptr_Z = ((real_type *) Z);
            tmpr = ptr_b[0] * a0r + ptr_b[1] * a0i;
            tmpi = ptr_b[1] * a0r - ptr_b[0] * a0i;
            /* Calculate first delay (output) */
            yn[0] = ptr_Z[0] + (tmpr * xn[0] - tmpi * xn[1]) / a0_mag;
            yn[1] = ptr_Z[1] + (tmpi * xn[0] + tmpr * xn[1]) / a0_mag;
            ptr_b += 2;
            ptr_a += 2;
            /* Fill in middle delays */
            for (n = 0; n < len_b - 2; n++) {
                tmpr = ptr_b[0] * a0r + ptr_b[1] * a0i;
                tmpi = ptr_b[1] * a0r - ptr_b[0] * a0i;
                ptr_Z[0] =
                    ptr_Z[2] + (tmpr * xn[0] - tmpi * xn[1]) / a0_mag;
                ptr_Z[1] =
                    ptr_Z[3] + (tmpi * xn[0] + tmpr * xn[1]) / a0_mag;
                tmpr = ptr_a[0] * a0r + ptr_a[1] * a0i;
                tmpi = ptr_a[1] * a0r - ptr_a[0] * a0i;
                ptr_Z[0] -= (tmpr * yn[0] - tmpi * yn[1]) / a0_mag;
                ptr_Z[1] -= (tmpi * yn[0] + tmpr * yn[1]) / a0_mag;
                ptr_b += 2;
                ptr_a += 2;
                ptr_Z += 2;
            }
            /* Calculate last delay */

            tmpr = ptr_b[0] * a0r + ptr_b[1] * a0i;
            tmpi = ptr_b[1] * a0r - ptr_b[0] * a0i;
            ptr_Z[0] = (tmpr * xn[0] - tmpi * xn[1]) / a0_mag;
            ptr_Z[1] = (tmpi * xn[0] + tmpr * xn[1]) / a0_mag;
            tmpr = ptr_a[0] * a0r + ptr_a[1] * a0i;
            tmpi = ptr_a[1] * a0r - ptr_a[0] * a0i;
            ptr_Z[0] -= (tmpr * yn[0] - tmpi * yn[1]) / a0_mag;
            ptr_Z[1] -= (tmpi * yn[0] + tmpr * yn[1]) / a0_mag;
        } else {
            tmpr = ptr_b[0] * a0r + ptr_b[1] * a0i;
            tmpi = ptr_b[1] * a0r - ptr_b[0] * a0i;
            yn[0] = (tmpr * xn[0] - tmpi * xn[1]) / a0_mag;
            yn[1] = (tmpi * xn[0] + tmpr * xn[1]) / a0_mag;
        }

        ptr_y += stride_Y;      /* Move to next input/output point */
        ptr_x += stride_X;

    }
    Py_END_ALLOW_THREADS
}
