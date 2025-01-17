///////////////////////////////////////////////
//
// **************************
// ** ENGLISH - 10/Jul/2017 **
//
// Project: libObfuscate v2.00
//
// This software is released under:
// * LGPL 3.0: "www.gnu.org/licenses/lgpl.html"
//
// You're free to copy, distribute and make commercial use
// of this software under the following conditions:
// * You cite the author and copyright owner: "www.embeddedsw.net"
// * You provide a link to the Homepage: "www.embeddedsw.net/libobfuscate.html"
//
///////////////////////////////////////////////

#ifndef MULTIBASE_H
#define MULTIBASE_H

// ** Thread-safe implementation

// ** Wrapping of implemented ciphers
// ** 128bit block size	(DATA_BLOCK_SIZE<<3)
// ** 2x 256bit keys	(MAX_PASSW_SIZE<<3)
// ** Electronic Codebook (ECB)

// "curAlg" = cipher function
// "passw" = password, byte[MAX_PASSW_SIZE]

#include "MultiBase_data.h"

extern	void Multi_single_setkey(MULTI_STATIC_DATA *pMsd,const ENUM_ALG curAlg,const BYTE *passw);
extern	void Multi_ECB_single_encrypt(const MULTI_STATIC_DATA *pMsd,const ENUM_ALG curAlg,const BYTE *inBuf,BYTE *outBuf);
extern	void Multi_ECB_single_decrypt(const MULTI_STATIC_DATA *pMsd,const ENUM_ALG curAlg,const BYTE *inBuf,BYTE *outBuf);

#endif
