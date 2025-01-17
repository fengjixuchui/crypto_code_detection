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

#ifndef CAST256_H
#define CAST256_H

// ** Thread-safe implementation

// ** Cast cipher
// ** 128bit block size
// ** 256bit key

extern	void Cast256_set_key(DWORD *l_key,const DWORD *in_key, const DWORD key_len);
extern	void Cast256_encrypt(const DWORD *l_key,const DWORD *in_blk, DWORD *out_blk);
extern	void Cast256_decrypt(const DWORD *l_key,const DWORD *in_blk, DWORD *out_blk);

#endif
