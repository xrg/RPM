/** \ingroup signature
 * \file rpmio/digest.c
 */

#include "system.h"

#include "rpmio/digest.h"

#include "debug.h"

#ifdef	SHA_DEBUG
#define	DPRINTF(_a)	fprintf _a
#else
#define	DPRINTF(_a)
#endif


/**
 * MD5/SHA1 digest private data.
 */
struct DIGEST_CTX_s {
    rpmDigestFlags flags;	/*!< Bit(s) to control digest operation. */
    HASHContext *hashctx;	/*!< Internal NSS hash context. */
};

DIGEST_CTX
rpmDigestDup(DIGEST_CTX octx)
{
    DIGEST_CTX nctx;
    nctx = memcpy(xcalloc(1, sizeof(*nctx)), octx, sizeof(*nctx));
    nctx->hashctx = HASH_Clone(octx->hashctx);
    if (nctx->hashctx == NULL) {
    	fprintf(stderr, "HASH_Clone failed\n");
    	exit(EXIT_FAILURE);  /* FIX: callers do not bother checking error return */
    }
    return nctx;
}

RPM_GNUC_PURE
static HASH_HashType getHashType(pgpHashAlgo hashalgo)
{
    switch (hashalgo) {
    case PGPHASHALGO_MD5:
	return HASH_AlgMD5;
	break;
    case PGPHASHALGO_MD2:
	return HASH_AlgMD2;
	break;
    case PGPHASHALGO_SHA1:
	return HASH_AlgSHA1;
	break;
    case PGPHASHALGO_SHA256:
	return HASH_AlgSHA256;
	break;
    case PGPHASHALGO_SHA384:
	return HASH_AlgSHA384;
	break;
    case PGPHASHALGO_SHA512:
	return HASH_AlgSHA512;
	break;
    case PGPHASHALGO_RIPEMD160:
    case PGPHASHALGO_TIGER192:
    case PGPHASHALGO_HAVAL_5_160:
    default:
	return HASH_AlgNULL;
	break;
    }
}

size_t
rpmDigestLength(pgpHashAlgo hashalgo)
{
    return HASH_ResultLen(getHashType(hashalgo));
}

DIGEST_CTX
rpmDigestInit(pgpHashAlgo hashalgo, rpmDigestFlags flags)
{
    HASH_HashType type;
    DIGEST_CTX ctx;

    if (rpmInitCrypto() < 0)
	return NULL;

    ctx = xcalloc(1, sizeof(*ctx));
    ctx->flags = flags;

    type = getHashType(hashalgo);
    if (type == HASH_AlgNULL) {
	free(ctx);
	return NULL;
    }

    ctx->hashctx = HASH_Create(type);
    if (ctx->hashctx == NULL) {
    	free(ctx);
    	return NULL;
    }

    HASH_Begin(ctx->hashctx);
    
DPRINTF((stderr, "*** Init(%x) ctx %p hashctx %p\n", flags, ctx, ctx->hashctx));
    return ctx;
}

int
rpmDigestUpdate(DIGEST_CTX ctx, const void * data, size_t len)
{
    size_t partlen;
    const unsigned char *ptr = data;

    if (ctx == NULL)
	return -1;

DPRINTF((stderr, "*** Update(%p,%p,%zd) hashctx %p \"%s\"\n", ctx, data, len, ctx->hashctx, ((char *)data)));
   partlen = ~(unsigned int)0xFF;
   while (len > 0) {
   	if (len < partlen) {
   		partlen = len;
   	}
	HASH_Update(ctx->hashctx, ptr, partlen);
	ptr += partlen;
	len -= partlen;
   }
   return 0;
}

int
rpmDigestFinal(DIGEST_CTX ctx, void ** datap, size_t *lenp, int asAscii)
{
    unsigned char * digest;
    unsigned int digestlen;

    if (ctx == NULL)
	return -1;
    digestlen = HASH_ResultLenContext(ctx->hashctx);
    digest = xmalloc(digestlen);

DPRINTF((stderr, "*** Final(%p,%p,%p,%zd) hashctx %p digest %p\n", ctx, datap, lenp, asAscii, ctx->hashctx, digest));
/* FIX: check rc */
    HASH_End(ctx->hashctx, digest, (unsigned int *) &digestlen, digestlen);

    /* Return final digest. */
    if (!asAscii) {
	if (lenp) *lenp = digestlen;
	if (datap) {
	    *datap = digest;
	    digest = NULL;
	}
    } else {
	if (lenp) *lenp = (2*digestlen) + 1;
	if (datap) {
	    const uint8_t * s = (const uint8_t *) digest;
	    *datap = pgpHexStr(s, digestlen);
	}
    }
    if (digest) {
	memset(digest, 0, digestlen);	/* In case it's sensitive */
	free(digest);
    }
    HASH_Destroy(ctx->hashctx);
    memset(ctx, 0, sizeof(*ctx));	/* In case it's sensitive */
    free(ctx);
    return 0;
}

void fdInitDigest(FD_t fd, pgpHashAlgo hashalgo, int flags)
{
    FDDIGEST_t fddig = fd->digests + fd->ndigests;
    if (fddig != (fd->digests + FDDIGEST_MAX)) {
	fd->ndigests++;
	fddig->hashalgo = hashalgo;
	fdstat_enter(fd, FDSTAT_DIGEST);
	fddig->hashctx = rpmDigestInit(hashalgo, flags);
	fdstat_exit(fd, FDSTAT_DIGEST, (ssize_t) 0);
    }
}

void fdUpdateDigests(FD_t fd, const unsigned char * buf, size_t buflen)
{
    int i;

    if (buf != NULL && buflen > 0)
    for (i = fd->ndigests - 1; i >= 0; i--) {
	FDDIGEST_t fddig = fd->digests + i;
	if (fddig->hashctx == NULL)
	    continue;
	fdstat_enter(fd, FDSTAT_DIGEST);
	(void) rpmDigestUpdate(fddig->hashctx, buf, buflen);
	fdstat_exit(fd, FDSTAT_DIGEST, (ssize_t) buflen);
    }
}

void fdFiniDigest(FD_t fd, pgpHashAlgo hashalgo,
		void ** datap,
		size_t * lenp,
		int asAscii)
{
    int imax = -1;
    int i;

    for (i = fd->ndigests - 1; i >= 0; i--) {
	FDDIGEST_t fddig = fd->digests + i;
	if (fddig->hashctx == NULL)
	    continue;
	if (i > imax) imax = i;
	if (fddig->hashalgo != hashalgo)
	    continue;
	fdstat_enter(fd, FDSTAT_DIGEST);
	(void) rpmDigestFinal(fddig->hashctx, datap, lenp, asAscii);
	fdstat_exit(fd, FDSTAT_DIGEST, (ssize_t) 0);
	fddig->hashctx = NULL;
	break;
    }
    if (i < 0) {
	if (datap) *datap = NULL;
	if (lenp) *lenp = 0;
    }

    fd->ndigests = imax;
    if (i < imax)
	fd->ndigests++;		/* convert index to count */
}


void fdStealDigest(FD_t fd, pgpDig dig)
{
    int i;
    for (i = fd->ndigests - 1; i >= 0; i--) {
        FDDIGEST_t fddig = fd->digests + i;
        if (fddig->hashctx != NULL)
        switch (fddig->hashalgo) {
        case PGPHASHALGO_MD5:
assert(dig->md5ctx == NULL);
            dig->md5ctx = fddig->hashctx;
            fddig->hashctx = NULL;
            break;
        case PGPHASHALGO_SHA1:
        case PGPHASHALGO_SHA256:
        case PGPHASHALGO_SHA384:
        case PGPHASHALGO_SHA512:
assert(dig->sha1ctx == NULL);
            dig->sha1ctx = fddig->hashctx;
            fddig->hashctx = NULL;
            break;
        default:
            break;
        }
    }
}
