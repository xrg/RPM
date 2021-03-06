#ifndef _RPMDIGEST_H
#define _RPMDIGEST_H

#include <nss.h>
#include <sechash.h>
#include <keyhi.h>
#include <cryptohi.h>

#include <rpm/rpmpgp.h>
#include "rpmio/base64.h"
#include "rpmio/rpmio_internal.h"


/** \ingroup rpmio
 * Values parsed from OpenPGP signature/pubkey packet(s).
 */
struct pgpDigParams_s {
    char * userid;
    uint8_t * hash;
    char * params[4];
    uint8_t tag;

    uint8_t version;		/*!< version number. */
    pgpTime_t time;		/*!< time that the key was created. */
    uint8_t pubkey_algo;		/*!< public key algorithm. */

    uint8_t hash_algo;
    uint8_t sigtype;
    uint8_t hashlen;
    uint8_t signhash16[2];
    pgpKeyID_t signid;
    uint8_t saved;
#define	PGPDIG_SAVED_TIME	(1 << 0)
#define	PGPDIG_SAVED_ID		(1 << 1)

};

/** \ingroup rpmio
 * Container for values parsed from an OpenPGP signature and public key.
 */
struct pgpDig_s {
    struct pgpDigParams_s signature;
    struct pgpDigParams_s pubkey;

    size_t nbytes;		/*!< No. bytes of plain text. */

    DIGEST_CTX sha1ctx;		/*!< (dsa) sha1 hash context. */
    DIGEST_CTX hdrsha1ctx;	/*!< (dsa) header sha1 hash context. */
    void * sha1;		/*!< (dsa) V3 signature hash. */
    size_t sha1len;		/*!< (dsa) V3 signature hash length. */

    DIGEST_CTX md5ctx;		/*!< (rsa) md5 hash context. */
    DIGEST_CTX hdrmd5ctx;	/*!< (rsa) header md5 hash context. */
    void * md5;			/*!< (rsa) V3 signature hash. */
    size_t md5len;		/*!< (rsa) V3 signature hash length. */

    /* DSA parameters */
    SECKEYPublicKey *dsa;
    SECItem *dsasig;

    /* RSA parameters */
    SECKEYPublicKey *rsa;
    SECItem *rsasig;
};

#endif /* _RPMDIGEST_H */
