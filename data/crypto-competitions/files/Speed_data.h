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

#ifndef SPEED_DATA_H
#define SPEED_DATA_H

// ** Thread-safe implementation

// ** Speed cipher
// ** 128bit block size
// ** 256bit key

#define	SPEED_KEY_LEN	256
#define	SPEED_DATA_LEN	128
#define	SPEED_NO_OF_RND	(20*8)	// rc6=20 rounds, rc6->speed=(*8)

#define SPEED_DATA_LEN_BYTE (SPEED_DATA_LEN/8)  /* no. of bytes in a p/c-text */
#define SPEED_KEY_LEN_BYTE  (SPEED_KEY_LEN/8)   /* no. of bytes in a key */

typedef BYTE speed_key [SPEED_KEY_LEN_BYTE];  /* for user key */
typedef BYTE speed_data[SPEED_DATA_LEN_BYTE]; /* for p/c-text */

typedef DWORD	speed_ikey [SPEED_NO_OF_RND];/* for round keys */
typedef DWORD	speed_idata[8];              /* for internal p/c-text */

#endif
