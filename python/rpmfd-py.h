#ifndef H_RPMFD_PY
#define H_RPMFD_PY

/** \ingroup py_c
 * \file python/rpmfd-py.h
 */

typedef struct rpmfdObject_s {
    PyObject_HEAD
    PyObject *md_dict;		/*!< to look like PyModuleObject */
    FD_t	fd;
} rpmfdObject;

extern PyTypeObject rpmfd_Type;

rpmfdObject * rpmfd_Wrap(FD_t fd);

#endif
